#pragma once
#include "../shader_compile.h"
#include "core/io/dir_access.h"
#include "core/io/file_access.h"
#include "tests/test_macros.h"

namespace TestHLSLProbe {
TEST_CASE("[Modules][GLSLang][Probe] Compile every vendored Jolt hair kernel") {
	const String dir = "thirdparty/jolt_physics/Jolt/Shaders";
	Ref<DirAccess> da = DirAccess::open(dir);
	REQUIRE(da.is_valid());
	PackedStringArray names;
	da->list_dir_begin();
	for (String f = da->get_next(); !f.is_empty(); f = da->get_next()) {
		if (f.ends_with(".hlsl")) {
			names.push_back(f);
		}
	}
	names.sort();
	int ok = 0, fail = 0;
	for (const String &n : names) {
		String src = FileAccess::get_file_as_string(dir.path_join(n));
		String err;
		Vector<uint8_t> spv = compile_hlsl_shader(RenderingDeviceCommons::SHADER_STAGE_COMPUTE, src, "main", { dir },
				RenderingDeviceCommons::SHADER_LANGUAGE_VULKAN_VERSION_1_1,
				RenderingDeviceCommons::SHADER_SPIRV_VERSION_1_3, &err);
		if (spv.is_empty()) {
			fail++;
			print_line(vformat("FAIL %s :: %s", n, err.substr(0, 160).replace("\n", " | ")));
		} else {
			ok++;
			// Scan OpCapability (17) / OpExtension (10). Capabilities that need an extension are
			// what "variable pointers" style validation failures come from.
			const uint32_t *w = reinterpret_cast<const uint32_t *>(spv.ptr());
			const uint32_t wc = spv.size() / 4;
			String caps, exts;
			for (uint32_t i = 5; i < wc;) {
				const uint32_t op = w[i] & 0xFFFF, len = w[i] >> 16;
				if (len == 0) { break; }
				if (op == 17 && len >= 2) { caps += itos(w[i + 1]) + " "; }
				if (op == 10 && len >= 2) {
					String e;
					const char *c = reinterpret_cast<const char *>(&w[i + 1]);
					for (uint32_t k = 0; k < (len - 1) * 4 && c[k]; k++) { e += String::chr(c[k]); }
					exts += e + " ";
				}
				i += len;
			}
			print_line(vformat("OK   %s :: %d bytes :: caps=[%s] exts=[%s]", n, spv.size(), caps.strip_edges(), exts.strip_edges()));
		}
	}
	print_line(vformat("PROBE TOTAL ok=%d fail=%d", ok, fail));
}
} // namespace TestHLSLProbe
