// G1 Tier-1 feasibility probe: MRT slot budget and per-format color-attachment support.
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <vulkan/vulkan.h>

typedef struct { VkFormat f; const char *n; const char *use; } F;
static F FMTS[] = {
    {VK_FORMAT_R8G8B8A8_UNORM,      "RGBA8_UNORM",       "gb_albedo, gb_orm"},
    {VK_FORMAT_R16G16B16A16_UNORM,  "RGBA16_UNORM",      "gb_normal (v1.3 merged)"},
    {VK_FORMAT_R16G16_UNORM,        "RG16_UNORM",        "gb_normal (v1.2, superseded)"},
    {VK_FORMAT_R16G16B16A16_SFLOAT, "RGBA16_SFLOAT",     "gb_emission"},
    {VK_FORMAT_R32_SFLOAT,          "R32_SFLOAT",        "gb_depth"},
    {VK_FORMAT_R32_UINT,            "R32_UINT",          "gb_objectid"},
    {VK_FORMAT_R16G16_SFLOAT,       "RG16_SFLOAT",       "gb_motion"},
    {VK_FORMAT_R8_UINT,             "R8_UINT",           "shading-model-ID (reserved slot)"},
};
#define NF (sizeof(FMTS)/sizeof(FMTS[0]))

int main(void) {
    VkApplicationInfo app = {VK_STRUCTURE_TYPE_APPLICATION_INFO};
    app.apiVersion = VK_API_VERSION_1_3;
    VkInstanceCreateInfo ici = {VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO};
    ici.pApplicationInfo = &app;
    VkInstance inst;
    if (vkCreateInstance(&ici, NULL, &inst) != VK_SUCCESS) return 1;
    uint32_t n = 0; vkEnumeratePhysicalDevices(inst, &n, NULL);
    VkPhysicalDevice *d = calloc(n, sizeof(*d));
    vkEnumeratePhysicalDevices(inst, &n, d);

    for (uint32_t i = 0; i < n; i++) {
        VkPhysicalDeviceProperties p;
        vkGetPhysicalDeviceProperties(d[i], &p);
        if (!strstr(p.deviceName, "GB10")) continue;
        printf("=== %s ===\n", p.deviceName);
        printf("RESULT maxColorAttachments=%u\n", p.limits.maxColorAttachments);
        printf("RESULT maxFragmentOutputAttachments=%u\n", p.limits.maxFragmentOutputAttachments);
        printf("RESULT maxFragmentCombinedOutputResources=%u\n", p.limits.maxFragmentCombinedOutputResources);
        for (size_t k = 0; k < NF; k++) {
            VkFormatProperties fp;
            vkGetPhysicalDeviceFormatProperties(d[i], FMTS[k].f, &fp);
            int ca = (fp.optimalTilingFeatures & VK_FORMAT_FEATURE_COLOR_ATTACHMENT_BIT) != 0;
            int bl = (fp.optimalTilingFeatures & VK_FORMAT_FEATURE_COLOR_ATTACHMENT_BLEND_BIT) != 0;
            int sm = (fp.optimalTilingFeatures & VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT) != 0;
            printf("RESULT fmt %-16s color_attach=%-3s blend=%-3s sampled=%-3s  (%s)\n",
                   FMTS[k].n, ca?"YES":"no", bl?"YES":"no", sm?"YES":"no", FMTS[k].use);
        }
    }
    return 0;
}
