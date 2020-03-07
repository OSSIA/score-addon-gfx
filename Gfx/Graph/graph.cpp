#include "graph.hpp"

#include "nodes.hpp"
#include "renderer.hpp"
#include "window.hpp"

#include <score/tools/Debug.hpp>

static void graphwalk(NodeModel* node, std::vector<NodeModel*>& list)
{
  for (auto inputs : node->input)
  {
    for (auto edge : inputs->edges)
    {
      if (!edge->source->node->addedToGraph)
      {
        list.push_back(edge->source->node);
        edge->source->node->addedToGraph = true;
      }
    }
  }
}

void Graph::setupOutputs(GraphicsApi graphicsApi)
{
#if QT_CONFIG(vulkan)
  if (graphicsApi == Vulkan && !vulkanInstanceCreated)
  {
#ifndef Q_OS_ANDROID
    vulkanInstance.setLayers(
        QByteArrayList() << "VK_LAYER_LUNARG_standard_validation");
#else
    vulkanInstance.setLayers(
        QByteArrayList() << "VK_LAYER_GOOGLE_threading"
                         << "VK_LAYER_LUNARG_parameter_validation"
                         << "VK_LAYER_LUNARG_object_tracker"
                         << "VK_LAYER_LUNARG_core_validation"
                         << "VK_LAYER_LUNARG_image"
                         << "VK_LAYER_LUNARG_swapchain"
                         << "VK_LAYER_GOOGLE_unique_objects");
#endif
    vulkanInstance.setExtensions(
        QByteArrayList() << "VK_KHR_get_physical_device_properties2");
    if (!vulkanInstance.create())
    {
      qWarning("Failed to create Vulkan instance, switching to OpenGL");
      graphicsApi = OpenGL;
    }
    vulkanInstanceCreated = true;
  }
#endif

#if __APPLE__
      graphicsApi = Metal;
#endif
#ifdef Q_OS_WIN
      graphicsApi = D3D11;
#endif

  for (auto output : outputs)
  {
    if (output->window)
    {
      output->window->canRender = false;
      output->window->onRender = [] {};
      ////output->window->state.hasSwapChain = false;
    }
  }

  for (auto node : nodes)
  {
    node->renderedNodes.clear();
  }

  for (auto& renderer : renderers)
  {
    renderer->release();

    for (auto rn : renderer->renderedNodes)
      delete rn;
  }

  renderers.clear();
  outputs.clear();

  for (auto node : nodes)
    if (auto out = dynamic_cast<OutputNode*>(node))
      outputs.push_back(out);

  renderers.reserve(std::max((int)16, (int)outputs.size()));
  int i = 0;
  for (auto output : outputs)
  {
    if (!output->window)
    {
      output->window = std::make_shared<Window>(graphicsApi);

#if QT_CONFIG(vulkan)
      if (graphicsApi == Vulkan)
        output->window->setVulkanInstance(&vulkanInstance);
#endif
      output->window->onWindowReady = [=] {
        output->window->state
            = RenderState::create(*output->window, graphicsApi);

        renderers.push_back(createRenderer(output, output->window->state));
      };
      output->window->onResize = [=] {
        for(auto it = this->renderers.begin(); it != this->renderers.end(); ++it)
        {
          auto& renderer = **it;
          if(renderer.state.window == output->window.get())
          {
            renderer.release();
          }
          (*it).reset();
          *it = createRenderer(output, output->window->state);
        }
      };
      output->window->resize(1280, 720);
      output->window->show();
    }
    else
    {
      renderers.push_back(createRenderer(output, output->window->state));
      // output->window->state.hasSwapChain = true;
    }

    output->window->onRender = [=] {
      if(auto r = output->window->state.renderer)
        r->render();
    };

    i++;
  }
}

void Graph::relinkGraph()
{
  for (auto& rptr : renderers)
  {
    auto& r = *rptr;
    for (auto& node : nodes)
      node->addedToGraph = false;

    assert(!r.nodes.empty());

    auto out = r.nodes.back();
    r.nodes.clear();
    r.nodes.push_back(out);

    r.renderedNodes.clear();

    auto& model_nodes = r.nodes;
    {
      // In which order do we want to render stuff
      int processed = 0;
      while (processed != model_nodes.size())
      {
        graphwalk(model_nodes[processed], model_nodes);
        processed++;
      }
      std::reverse(model_nodes.begin(), model_nodes.end());

      if (model_nodes.size() > 1)
      {
        for (auto node : model_nodes)
        {
          auto rn = node->renderedNodes[&r];
          if (!rn)
          {
            rn = node->createRenderer();
            if (node != model_nodes.back())
            {
              rn->createRenderTarget(r.state);
            }
            node->renderedNodes[&r] = rn;
            rn->init(r);
          }
          else
          {
            rn->releaseWithoutRenderTarget(r);
            rn->init(r);
          }
          SCORE_ASSERT(rn);
          r.renderedNodes.push_back(rn);
        }
      }
      else if (model_nodes.size() == 1)
      {
        auto rn = model_nodes[0]->renderedNodes[&r];
        assert(rn);
        rn->release(r);
      }
    }
    r.state.window->canRender = r.renderedNodes.size() > 1;
  }
}

std::shared_ptr<Renderer> Graph::createRenderer(OutputNode* output, RenderState state)
{
  auto ptr = std::make_shared<Renderer>();
  for (auto& node : nodes)
    node->addedToGraph = false;
  Renderer& r = *ptr;
  output->window->state.renderer = ptr.get();
  r.state = std::move(state);

  auto& model_nodes = r.nodes;
  {
    model_nodes.push_back(output);

    // In which order do we want to render stuff
    int processed = 0;
    while (processed != model_nodes.size())
    {
      graphwalk(model_nodes[processed], model_nodes);
      processed++;
    }
    std::reverse(model_nodes.begin(), model_nodes.end());

    // Now we have the nodes in the order in which they are going to
    // be processed

    // We create renderers for each of them
    for (auto node : model_nodes)
    {
      r.renderedNodes.push_back(node->createRenderer());
    }
  }

  // For each, we create a render target
  for (std::size_t i = 0; i < r.renderedNodes.size() - 1; i++)
  {
    auto node = r.renderedNodes[i];
    node->createRenderTarget(r.state);
  }

  // Except the last one which is going to render to screen
  r.renderedNodes.back()->setScreenRenderTarget(r.state);

  output->window->canRender = r.renderedNodes.size() > 1;
  {
    // Register the rendered nodes with their parents
    for (auto rn : r.renderedNodes)
    {
      const_cast<NodeModel&>(rn->node).renderedNodes[&r] = rn;
    }

    r.init(*r.state.rhi);

    if (model_nodes.size() > 1)
    {
      for (auto rn : r.renderedNodes)
        rn->init(r);
    }
  }

  return ptr;
}

Graph::~Graph()
{
  for (auto& renderer : renderers)
  {
    renderer->release();
  }

  for (auto out : outputs)
  {
    out->window.reset();
  }
}
