#include <jsmodules/JSCModuleExtension.h>
#include <jquick_config.h>
#include "MinecraftRenderer/JSMinecraftRenderer.hpp"

using namespace JQUTIL_NS;

static std::vector<std::string> exportList = {
    "MinecraftRenderer"
};

static int module_init(JSContext *ctx, JSModuleDef *m)
{
    auto env = JQModuleEnv::CreateModule(ctx, m, "Minecraft");

    env->setModuleExport("MinecraftRenderer", createMinecraftRenderer(env.get()));

    env->setModuleExportDone(JS_UNDEFINED, exportList);
    return 0;
}

DEF_MODULE_LOAD_FUNC_EXPORT(Minecraft, module_init, exportList)

extern "C" JQUICK_EXPORT void custom_init_jsapis()
{
    registerCModuleLoader("Minecraft", &Minecraft_module_load);
}
