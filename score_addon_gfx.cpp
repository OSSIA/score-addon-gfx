#include "score_addon_gfx.hpp"

#include <score/plugins/FactorySetup.hpp>

#include <Gfx/CommandFactory.hpp>
#include <Gfx/Filter/Executor.hpp>
#include <Gfx/Filter/Inspector.hpp>
#include <Gfx/Filter/Layer.hpp>
#include <Gfx/Filter/Process.hpp>
#include <Gfx/GfxApplicationPlugin.hpp>
#include <Gfx/GfxDevice.hpp>
#include <Gfx/TexturePort.hpp>
#include <Gfx/Video/Executor.hpp>
#include <Gfx/Video/Inspector.hpp>
#include <Gfx/Video/Layer.hpp>
#include <Gfx/Video/Process.hpp>
#include <score_addon_gfx_commands_files.hpp>
#include <score_plugin_engine.hpp>

score_addon_gfx::score_addon_gfx() {}

score_addon_gfx::~score_addon_gfx() {}

std::vector<std::unique_ptr<score::InterfaceBase>> score_addon_gfx::factories(
    const score::ApplicationContext& ctx,
    const score::InterfaceKey& key) const
{
  return instantiate_factories<
      score::ApplicationContext,
      FW<Device::ProtocolFactory, Gfx::GfxProtocolFactory>,

      FW<Process::ProcessModelFactory,
         Gfx::Filter::ProcessFactory,
         Gfx::Video::ProcessFactory>,
      FW<Process::LayerFactory,
         Gfx::Filter::LayerFactory,
         Gfx::Video::LayerFactory>,
      FW<Execution::ProcessComponentFactory,
         Gfx::Filter::ProcessExecutorComponentFactory,
         Gfx::Video::ProcessExecutorComponentFactory>,
      FW<Inspector::InspectorWidgetFactory,
         Gfx::Filter::InspectorFactory,
         Gfx::Video::InspectorFactory>,
      FW<Process::PortFactory,
         Gfx::TextureInletFactory,
         Gfx::TextureOutletFactory>,
      FW<Process::ProcessDropHandler,
         Gfx::Filter::DropHandler,
         Gfx::Video::DropHandler>,
      FW<Library::LibraryInterface,
         Gfx::Filter::LibraryHandler,
         Gfx::Video::LibraryHandler>
      >(ctx, key);
}

score::GUIApplicationPlugin* score_addon_gfx::make_guiApplicationPlugin(
    const score::GUIApplicationContext& app)
{
  return new Gfx::ApplicationPlugin{app};
}

std::pair<const CommandGroupKey, CommandGeneratorMap>
score_addon_gfx::make_commands()
{
  using namespace Gfx;
  std::pair<const CommandGroupKey, CommandGeneratorMap> cmds{
      CommandFactoryName(), CommandGeneratorMap{}};

  ossia::for_each_type<
#include <score_addon_gfx_commands.hpp>
      >(score::commands::FactoryInserter{cmds.second});

  return cmds;
}
auto score_addon_gfx::required() const -> std::vector<score::PluginKey>
{
  return {score_plugin_engine::static_key()};
}

#include <score/plugins/PluginInstances.hpp>
SCORE_EXPORT_PLUGIN(score_addon_gfx)
