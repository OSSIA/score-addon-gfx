#pragma once
#include <ossia/dataflow/port.hpp>
#include <ossia/dataflow/token_request.hpp>
#include <ossia/detail/flicks.hpp>
#include <ossia/detail/hash_map.hpp>
#include <ossia/network/value/value_conversion.hpp>

#include <Gfx/Graph/window.hpp>
#include <concurrentqueue.h>
namespace Gfx
{

struct port_index
{
  int32_t node{};
  int32_t port{};

  auto operator==(port_index other) const noexcept
  {
    return node == other.node && port == other.port;
  }
  auto operator!=(port_index other) const noexcept
  {
    return node != other.node || port != other.port;
  }
  auto operator<(port_index other) const noexcept
  {
    return node < other.node || (node == other.node && port < other.port);
  }
};

using gfx_input = std::variant<ossia::value, ossia::audio_vector>;

struct gfx_message
{
  int32_t node_id{};
  ossia::token_request token{};
  std::vector<std::vector<gfx_input>> inputs;
};

struct gfx_view_node
{
  std::unique_ptr<NodeModel> impl;

  void process(const ossia::token_request& tk)
  {
    ProcessUBO& UBO = impl->standardUBO;
    auto prev_time = UBO.time;

    UBO.time = tk.date.impl / ossia::flicks_per_second<double>;
    UBO.timeDelta = UBO.time - prev_time;

    if (tk.parent_duration.impl > 0)
      UBO.progress = tk.date.impl / double(tk.parent_duration.impl);
    else
      UBO.progress = 0.;

    UBO.passIndex = 0;
  }

  void process(int32_t port, const ossia::value& v)
  {
    struct vec_visitor
    {
      const std::vector<ossia::value>& v;
      void operator()(std::monostate) const noexcept {}
      void operator()(float& val) const noexcept
      {
        if (!v.empty())
          val = ossia::convert<float>(v[0]);
      }
      void operator()(ossia::vec2f& val) const noexcept
      {
        val = ossia::convert<ossia::vec2f>(v);
      }
      void operator()(ossia::vec3f& val) const noexcept
      {
        val = ossia::convert<ossia::vec3f>(v);
      }
      void operator()(ossia::vec4f& val) const noexcept
      {
        val = ossia::convert<ossia::vec4f>(v);
      }
      void operator()(image& val) const noexcept {}
    };

    struct value_visitor
    {
      Types type{};
      void* value{};
      void operator()() const noexcept {}
      void operator()(ossia::impulse) const noexcept {}
      void operator()(int v) const noexcept {
        switch(type)
        {
          case Types::Int:
            memcpy(value, &v, 4);
            break;
          case Types::Float:
          {
            float fv = v;
            memcpy(value, &fv, 4);
            break;
          }
        }
      }
      void operator()(float v) const noexcept {
        switch(type)
        {
          case Types::Int:
          {
            int iv = v;
            memcpy(value, &iv, 4);
            break;
          }
          case Types::Float:
          {
            memcpy(value, &v, 4);
            break;
          }
        }
      }
      void operator()(bool v) const noexcept {
        switch(type)
        {
          case Types::Int:
          {
            int iv = v;
            memcpy(value, &iv, 4);
            break;
          }
          case Types::Float:
          {
            float fv = v;
            memcpy(value, &fv, 4);
            break;
          }
        }
      }
      void operator()(char v) const noexcept {
        switch(type)
        {
          case Types::Int:
          {
            int iv = v;
            memcpy(value, &iv, 4);
            break;
          }
          case Types::Float:
          {
            float fv = v;
            memcpy(value, &fv, 4);
            break;
          }
        }
      }
      void operator()(const std::string& v) const noexcept {}
      void operator()(ossia::vec2f v) const noexcept { memcpy(value, v.data(), 8); }
      void operator()(ossia::vec3f v) const noexcept { memcpy(value, v.data(), 12); }
      void operator()(ossia::vec4f v) const noexcept { memcpy(value, v.data(), 16); }
      void operator()(const std::vector<ossia::value>& v) const noexcept
      {
        // TODOstd::visit(vec_visitor{v}, value);
      }
    };

    assert(int(impl->input.size()) > port);

    auto& in = impl->input[port];
    v.apply(value_visitor{in->type, in->value});
    impl->materialChanged++;
  }

  void process(int32_t port, const ossia::audio_vector& v)
  {
    if(v.empty() || v[0].empty())
      return;

    assert(int(impl->input.size()) > port);
    auto& in = impl->input[port];
    assert(in->type == Types::Audio);
    AudioTexture& tex = *(AudioTexture*) in->value;

    tex.channels = v.size();
    tex.data.clear();
    //if(tex.fixedSize)
    {
      // TODO

    }
    // else
    {
      tex.data.resize(v.size() * v[0].size());

      float* sample = tex.data.data();
      for(auto& chan : v)
      {
        for(float in : chan)
        {
          (*sample) = 0.5f + in / 2.f;
          sample++;
        }
      }
    }
  }
};

class gfx_window_context : public QObject
{
  GraphicsApi m_api{};
  int32_t index{};

  ossia::fast_hash_map<int32_t, gfx_view_node> nodes;

  Graph* m_graph{};

  QThread m_thread;

  bool must_recompute = false;

public:
  moodycamel::ConcurrentQueue<gfx_message> tick_messages;

  gfx_window_context()
  {
#if defined(Q_OS_WIN)
    m_api = D3D11;
#elif defined(Q_OS_DARWIN)
    m_api = Metal;
#elif QT_CONFIG(vulkan)
    m_api = Vulkan;
#else
    m_api = OpenGL;
#endif
    m_api = OpenGL;

    m_graph = new Graph;

    if(m_api == Vulkan)
    {
      //:startTimer(16);
      moveToThread(&m_thread);
      m_thread.start();
    }

    QMetaObject::invokeMethod(this, [this] { startTimer(16); },
    Qt::QueuedConnection);
  }

  ~gfx_window_context()
  {
    if(m_thread.isRunning())
      m_thread.exit(0);
    m_thread.wait();
  }

  int32_t register_node(std::unique_ptr<NodeModel> node)
  {
    auto next = index;
    m_graph->addNode(node.get());
    nodes[next] = {std::move(node)};

    index++;

    recompute_graph();
    return next;
  }

  void unregister_node(int32_t idx)
  {
    // Remove all edges involving that node
    for (auto it = this->edges.begin(); it != this->edges.end();)
    {
      if (it->first.node == idx || it->second.node == idx)
        it = this->edges.erase(it);
      else
        ++it;
    }
    auto it = nodes.find(idx);
    if (it != nodes.end())
    {
      m_graph->removeNode(it->second.impl.get());
      recompute_graph();
    }
    nodes.erase(it);

    recompute_graph();
  }

  void recompute_edges()
  {
    for (auto edge : m_graph->edges)
    {
      delete edge;
    }

    m_graph->edges.clear();
    for (auto edge : edges)
    {
      auto source_node_it = this->nodes.find(edge.first.node);
      assert(source_node_it != this->nodes.end());
      auto sink_node_it = this->nodes.find(edge.second.node);
      assert(sink_node_it != this->nodes.end());

      assert(source_node_it->second.impl);
      assert(sink_node_it->second.impl);

      auto source_port = source_node_it->second.impl->output[edge.first.port];
      auto sink_port = sink_node_it->second.impl->input[edge.second.port];

      auto e = new Edge{source_port, sink_port};
      m_graph->edges.push_back(e);
    }
  }
  void recompute_graph()
  {
    recompute_edges();
    m_graph->setupOutputs(m_api);
    must_recompute = false;
  }

  void recompute_connections()
  {
    recompute_edges();
    // m_graph->setupOutputs(m_api);
    m_graph->relinkGraph();
  }

  void update_inputs()
  {
    struct node_vis
    {
      gfx_window_context& self;
      gfx_view_node& node;
      port_index sink{};

      void operator()(ossia::value&& v) const noexcept
      {
        node.process(sink.port, std::move(v));
      }

      void operator()(ossia::audio_vector&& v) const noexcept
      {
        node.process(sink.port, std::move(v));
      }
    };

    gfx_message msg;
    while (tick_messages.try_dequeue(msg))
    {
      if (auto it = nodes.find(msg.node_id); it != nodes.end())
      {
        auto& node = it->second;
        node.process(msg.token);

        node_vis v{*this, node};
        int32_t p = 0;
        for (std::vector<gfx_input>& dat : msg.inputs)
        {
          v.sink = port_index{msg.node_id, p};
          for (gfx_input& m : dat)
            std::visit(v, std::move(m));

          p++;
        }
      }
    }
  }

  void timerEvent(QTimerEvent*) override
  {
    update_inputs();

    if (edges_changed)
    {
      {
        std::lock_guard l{edges_lock};
        std::swap(edges, new_edges);
      }
      recompute_connections();
      edges_changed = false;
    }
  }

  std::mutex edges_lock;
  ossia::flat_set<std::pair<port_index, port_index>> new_edges;
  ossia::flat_set<std::pair<port_index, port_index>> edges;
  std::atomic_bool edges_changed{};
};

}
