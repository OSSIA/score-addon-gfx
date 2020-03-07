#pragma once
#include "renderstate.hpp"
#include "uniforms.hpp"

#include <ossia/detail/flat_map.hpp>

#include <QShaderBaker>

#include <algorithm>
#include <vector>
#include <optional>
#include <unordered_map>

class NodeModel;
struct Port;
struct Edge;
class Renderer;
struct AudioTexture
{
  std::unordered_map<Renderer*, std::pair<QRhiSampler*, QRhiTexture*>> samplers;

  std::vector<float> data;
  int channels{};
  int fixedSize{0};
};

struct Port
{
  NodeModel* node{};
  void* value{};
  Types type{};
  std::vector<Edge*> edges;
};

struct Edge
{
  Edge(Port* source, Port* sink) : source{source}, sink{sink}
  {
    source->edges.push_back(this);
    sink->edges.push_back(this);
  }

  ~Edge()
  {
    if (auto it = std::find(source->edges.begin(), source->edges.end(), this);
        it != source->edges.end())
      source->edges.erase(it);
    if (auto it = std::find(sink->edges.begin(), sink->edges.end(), this);
        it != sink->edges.end())
      sink->edges.erase(it);
  }

  Port* source{};
  Port* sink{};
};

struct Sampler
{
  QRhiSampler* sampler{};
  QRhiTexture* texture{};
};

#if defined(_MSC_VER)
#pragma pack(push, 1)
#endif
struct
#if defined(__GNUC__) || defined(__clang__)
    __attribute__((packed))
#endif
    ProcessUBO
{
  float time{};
  float timeDelta{};
  float progress{};

  int32_t passIndex{};
  int32_t frameIndex{};

  float date[4]{0.f, 0.f, 0.f, 0.f};
  float mouse[4]{0.5f, 0.5f, 0.5f, 0.5f};
  float channelTime[4]{0.5f, 0.5f, 0.5f, 0.5f};

  float sampleRate{};
};
#if defined(_MSC_VER)
#pragma pack()
#endif

struct Renderer;
class RenderedNode;
class NodeModel
{
  friend class RenderedNode;

public:
  explicit NodeModel();
  explicit NodeModel(QString frag);
  explicit NodeModel(QString vert, QString frag);
  virtual ~NodeModel();

  virtual RenderedNode* createRenderer() const noexcept;

  std::vector<Port*> input;
  std::vector<Port*> output;

  ossia::flat_map<Renderer*, RenderedNode*> renderedNodes;

  ProcessUBO standardUBO{};

protected:
  void setShaders(QString vert, QString frag);

  QShader m_vertexS;
  QShader m_fragmentS;

  std::unique_ptr<char[]> m_materialData;

  friend class RenderedNode;
public:
  int64_t materialChanged{0};
  bool addedToGraph{};
};

class RenderedNode
{
public:
  RenderedNode(const NodeModel& node) noexcept : node{node} {}

  virtual ~RenderedNode() {}
  const NodeModel& node;

  QRhiTexture* m_texture{};
  QRhiRenderTarget* m_renderTarget{};
  QRhiRenderPassDescriptor* m_renderPass{};

  std::vector<Sampler> m_samplers;

  // Pipeline
  QRhiShaderResourceBindings* m_srb{};
  QRhiGraphicsPipeline* m_ps{};

  QRhiBuffer* m_processUBO{};

  QRhiBuffer* m_materialUBO{};
  int m_materialSize{};
  int64_t materialChangedIndex{-1};

  friend struct Graph;
  friend struct Renderer;

  void createRenderTarget(const RenderState& state);
  void setScreenRenderTarget(const RenderState& state);

  virtual std::optional<QSize> renderTargetSize() const noexcept;
  // Render loop
  virtual void customInit(Renderer& renderer);
  void init(Renderer& renderer);

  virtual void customUpdate(Renderer& renderer, QRhiResourceUpdateBatch& res);
  void update(Renderer& renderer, QRhiResourceUpdateBatch& res);

  virtual void customRelease(Renderer&);
  void release(Renderer&);
  void releaseWithoutRenderTarget(Renderer&);

  QRhiGraphicsPipeline* pipeline() { return m_ps; }
  QRhiShaderResourceBindings* resources() { return m_srb; }
};
