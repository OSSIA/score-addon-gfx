#pragma once
extern "C"
{
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/pixdesc.h>
#include <libswscale/swscale.h>
}
#include <QElapsedTimer>

#include <atomic>
#include <functional>
#include <mutex>
#include <string>
#include <thread>
#include <list>

#include <condition_variable>

class video_decoder
{
public:
  video_decoder() noexcept = default;

  ~video_decoder() noexcept { close_file(); }

  bool load(const std::string& inputFile, double fps_unused) noexcept
  {
    close_file();

    if (avformat_open_input(
            &m_formatContext, inputFile.c_str(), nullptr, nullptr)
        != 0)
    {
      close_file();
      return false;
    }

    if (avformat_find_stream_info(m_formatContext, nullptr) < 0)
    {
      close_file();
      return false;
    }

    if (!open_stream())
    {
      close_file();
      return false;
    }

    m_running.store(true, std::memory_order_release);
    m_thread = std::thread{[this] { this->buffer_thread(); }};

    return true;
  }

  auto width() const noexcept { return m_width; }
  auto height() const noexcept { return m_height; }
  auto fps() const noexcept { return m_rate; }

  auto pixel_format() const noexcept { return m_pixel_format; }

  void seek(int64_t dts) { m_seekTo = dts; }

  AVFrame* dequeue_frame() noexcept
  {
    AVFrame* f{};
    m_framesMutex.lock();
    if (auto to_discard = m_discardUntil)
    {
      while (!m_frames.empty() && m_frames.front() != to_discard)
      {
        f = m_frames.front();
        av_frame_free(&f);
        m_frames.pop_front();
      }

      if (!m_frames.empty())
        m_frames.pop_front();

      m_discardUntil = nullptr;
      m_framesMutex.unlock();
      return to_discard;
    }

    if (!m_frames.empty())
    {
      f = m_frames.front();
      m_frames.pop_front();
    }
    m_framesMutex.unlock();
    return f;
  }

private:
  void buffer_thread() noexcept
  {
    while (m_running.load(std::memory_order_acquire))
    {
      if (int64_t seek = m_seekTo.exchange(-1); seek >= 0)
      {
        seek_impl(seek);
      }
      else
      {
        bool mustRead{};
        {
          std::lock_guard _{m_framesMutex};
          mustRead = m_frames.size() < frames_to_buffer;
        }

        if (mustRead)
        {
          if (auto f = read_frame_impl())
          {
            m_last_dts = f->pkt_dts;
            std::lock_guard _{m_framesMutex};
            m_frames.push_back(f);
          }
        }
      }
    }
  }

  void close_file() noexcept
  {
    m_running.store(false, std::memory_order_release);

    if (m_thread.joinable())
      m_thread.join();

    close_video();

    if (m_formatContext)
    {
      avformat_close_input(&m_formatContext);
      m_formatContext = nullptr;
    }
  }

  bool seek_impl(int64_t dts) noexcept
  {
    {
      std::lock_guard _{m_framesMutex};
      if (m_frames.empty() || m_frames.front()->pkt_dts == dts)
      {
        return false;
      }
    }

    int flags = AVSEEK_FLAG_FRAME;
    if (dts < this->m_last_dts)
    {
      flags |= AVSEEK_FLAG_BACKWARD;
    }

    if (av_seek_frame(m_formatContext, m_stream, dts, flags))
    {
      printf("\nFailed to seek for time %d", dts);
      return false;
    }

    avcodec_flush_buffers(m_codecContext);

    int got_frame = 0;
    AVPacket pkt{};
    AVFrame* f = av_frame_alloc();
    do
    {
      if (av_read_frame(m_formatContext, &pkt) == 0)
      {
        got_frame = enqueue_frame(&pkt, f);
        av_packet_unref(&pkt);
      }
      else
      {
        break;
      }
    } while (!(got_frame && f->pkt_dts >= dts));

    m_last_dts = f->pkt_dts;
    {
      std::lock_guard _{m_framesMutex};
      m_discardUntil = f;
      m_frames.push_back(f);
    }
    return true;
  }

  AVFrame* read_frame_impl() noexcept
  {
    AVFrame* res = nullptr;

    if (m_stream != -1)
    {
      AVFrame* frame = av_frame_alloc();
      AVPacket packet;
      memset(&packet, 0, sizeof(AVPacket));
      bool ok = false;

      while (av_read_frame(m_formatContext, &packet) >= 0)
      {
        if (packet.stream_index == m_stream)
        {
          if (enqueue_frame(&packet, frame))
          {
            res = frame;
            ok = true;
          }

          av_packet_unref(&packet);
          packet = AVPacket{};
          break;
        }

        av_packet_unref(&packet);
        packet = AVPacket{};
      }

      if (!ok)
      {
        av_frame_free(&frame);
        res = nullptr;
      }
    }
    return res;
  }

  bool open_stream() noexcept
  {
    bool res = false;

    if (!m_formatContext)
      return res;

    m_stream = -1;

    for (unsigned int i = 0; i < m_formatContext->nb_streams; i++)
    {
      if (m_formatContext->streams[i]->codec->codec_type == AVMEDIA_TYPE_VIDEO)
      {
        m_stream = i;
        m_codecContext = m_formatContext->streams[i]->codec;
        m_codec = avcodec_find_decoder(m_codecContext->codec_id);

        if (m_codec)
        {
          m_pixel_format = m_codecContext->pix_fmt;
          res = !(avcodec_open2(m_codecContext, m_codec, nullptr) < 0);
          m_width = m_codecContext->coded_width;
          m_height = m_codecContext->coded_height;
          m_rate = av_q2d(m_formatContext->streams[i]->avg_frame_rate);
        }

        break;
      }
    }

    if (!res)
    {
      close_video();
    }

    return res;
  }

  void close_video() noexcept
  {
    if (m_codecContext)
    {
      avcodec_close(m_codecContext);
      m_codecContext = nullptr;
      m_codec = nullptr;

      m_stream = -1;
    }
  }

  bool enqueue_frame(const AVPacket* pkt, AVFrame* frame) noexcept
  {
    int got_picture_ptr = 0;

    if (m_codecContext && pkt && frame)
    {
      int ret = avcodec_send_packet(m_codecContext, pkt);
      if (ret < 0)
        return ret == AVERROR_EOF ? 0 : ret;

      ret = avcodec_receive_frame(m_codecContext, frame);
      if (ret < 0 && ret != AVERROR(EAGAIN) && ret != AVERROR_EOF)
        return ret;

      if (ret >= 0)
        got_picture_ptr = 1;
    }

    return got_picture_ptr == 1;
  }

  static const constexpr int frames_to_buffer = 16;

  std::thread m_thread;

  std::list<AVFrame*> m_frames;
  std::mutex m_framesMutex;

  AVFormatContext* m_formatContext{};
  AVCodecContext* m_codecContext{};
  AVCodec* m_codec{};
  int m_stream{-1};

  AVPixelFormat m_pixel_format{};

  int m_width{};
  int m_height{};
  double m_rate{};

  AVFrame* m_discardUntil{};
  std::atomic_int64_t m_seekTo = -1;
  int64_t m_last_dts = 0;

  std::atomic_bool m_running{};
};
