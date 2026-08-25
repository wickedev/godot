/**************************************************************************/
/*  register_types.cpp                                                    */
/**************************************************************************/
/*                         This file is part of:                          */
/*                             GODOT ENGINE                               */
/*                        https://godotengine.org                         */
/**************************************************************************/
/* Copyright (c) 2014-present Godot Engine contributors (see AUTHORS.md). */
/* Copyright (c) 2007-2014 Juan Linietsky, Ariel Manzur.                  */
/*                                                                        */
/* Permission is hereby granted, free of charge, to any person obtaining  */
/* a copy of this software and associated documentation files (the        */
/* "Software"), to deal in the Software without restriction, including    */
/* without limitation the rights to use, copy, modify, merge, publish,    */
/* distribute, sublicense, and/or sell copies of the Software, and to     */
/* permit persons to whom the Software is furnished to do so, subject to  */
/* the following conditions:                                              */
/*                                                                        */
/* The above copyright notice and this permission notice shall be         */
/* included in all copies or substantial portions of the Software.        */
/*                                                                        */
/* THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,        */
/* EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF     */
/* MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. */
/* IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY   */
/* CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT,   */
/* TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE      */
/* SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.                 */
/**************************************************************************/

#include "register_types.h"

#include "shader_compile.h"

#include "core/config/engine.h"
#include "core/io/file_access.h"

#ifdef D3D12_ENABLED
#include "core/os/os.h"
#endif

GODOT_GCC_WARNING_PUSH_AND_IGNORE("-Wshadow")

#include <glslang/Public/ResourceLimits.h>
#include <glslang/Public/ShaderLang.h>
#include <glslang/SPIRV/GlslangToSpv.h>

GODOT_GCC_WARNING_POP

// Resolves #include through Godot's VFS rather than the OS, so res:// and engine-published paths
// both work. glslang's own DirStackFileIncluder lives in StandAlone/ and is not vendored.
class GodotShaderIncluder : public glslang::TShader::Includer {
	Vector<String> search_paths;

	IncludeResult *_resolve(const char *p_header, const String &p_relative_to) {
		Vector<String> candidates;
		if (!p_relative_to.is_empty()) {
			candidates.push_back(p_relative_to.path_join(String::utf8(p_header)));
		}
		for (const String &root : search_paths) {
			candidates.push_back(root.path_join(String::utf8(p_header)));
		}

		for (const String &candidate : candidates) {
			if (!FileAccess::exists(candidate)) {
				continue;
			}
			const CharString text = FileAccess::get_file_as_string(candidate).utf8();
			// glslang keeps the buffer until releaseInclude(), so it has to outlive this call.
			char *buffer = memnew_arr(char, text.length() + 1);
			memcpy(buffer, text.get_data(), text.length() + 1);
			return memnew(IncludeResult(std::string(candidate.utf8().get_data()), buffer, text.length(), buffer));
		}
		return nullptr;
	}

public:
	explicit GodotShaderIncluder(const Vector<String> &p_search_paths) :
			search_paths(p_search_paths) {}

	IncludeResult *includeLocal(const char *header, const char *includer, size_t) override {
		return _resolve(header, String::utf8(includer).get_base_dir());
	}

	IncludeResult *includeSystem(const char *header, const char *, size_t) override {
		return _resolve(header, String());
	}

	void releaseInclude(IncludeResult *result) override {
		if (result != nullptr) {
			memdelete_arr(static_cast<char *>(result->userData));
			memdelete(result);
		}
	}
};

static Vector<uint8_t> compile_shader(glslang::EShSource p_source_language, RenderingDeviceCommons::ShaderStage p_stage, const String &p_source_code, const String &p_entry_point, const Vector<String> &p_include_paths, RenderingDeviceCommons::ShaderLanguageVersion p_language_version, RenderingDeviceCommons::ShaderSpirvVersion p_spirv_version, String *r_error) {
	Vector<uint8_t> ret;
	EShLanguage stages[RenderingDeviceCommons::SHADER_STAGE_MAX] = {
		EShLangVertex,
		EShLangFragment,
		EShLangTessControl,
		EShLangTessEvaluation,
		EShLangCompute,
		EShLangRayGen,
		EShLangAnyHit,
		EShLangClosestHit,
		EShLangMiss,
		EShLangIntersect,
	};

	int ClientInputSemanticsVersion = 100; // maps to, say, #define VULKAN 100

	// The enum values can be converted directly.
	glslang::EShTargetClientVersion ClientVersion = (glslang::EShTargetClientVersion)p_language_version;
	glslang::EShTargetLanguageVersion TargetVersion = (glslang::EShTargetLanguageVersion)p_spirv_version;

	glslang::TShader shader(stages[p_stage]);
	CharString cs = p_source_code.utf8();
	const char *cs_strings = cs.get_data();
	std::string preamble = "";

	shader.setStrings(&cs_strings, 1);
	shader.setEnvInput(p_source_language, stages[p_stage], glslang::EShClientVulkan, ClientInputSemanticsVersion);
	shader.setEnvClient(glslang::EShClientVulkan, ClientVersion);
	shader.setEnvTarget(glslang::EShTargetSpv, TargetVersion);

	if (p_source_language == glslang::EShSourceHlsl) {
		const CharString entry_point = p_entry_point.utf8();
		shader.setEntryPoint(entry_point.get_data());
		shader.setSourceEntryPoint(entry_point.get_data());
		// HLSL has no binding decorations, so without this every resource lands on binding 0 and
		// the module fails validation the moment a shader declares more than one.
		shader.setAutoMapBindings(true);
		shader.setAutoMapLocations(true);
		shader.setHlslIoMapping(true);
	}

	if (!preamble.empty()) {
		shader.setPreamble(preamble.c_str());
	}

	bool generate_spirv_debug_info = Engine::get_singleton()->is_generate_spirv_debug_info_enabled();
#ifdef D3D12_ENABLED
	if (OS::get_singleton()->get_current_rendering_driver_name() == "d3d12") {
		// SPIRV to DXIL conversion does not support debug info.
		generate_spirv_debug_info = false;
	}
#endif

	EShMessages messages = (EShMessages)(EShMsgSpvRules | EShMsgVulkanRules);
	if (p_source_language == glslang::EShSourceHlsl) {
		messages = (EShMessages)(messages | EShMsgReadHlsl);
	}
	if (generate_spirv_debug_info) {
		messages = (EShMessages)(messages | EShMsgDebugInfo);
	}
	const int DefaultVersion = 100;

	//parse
	GodotShaderIncluder includer(p_include_paths);
	if (!shader.parse(GetDefaultResources(), DefaultVersion, false, messages, includer)) {
		if (r_error) {
			(*r_error) = "Failed parse:\n";
			(*r_error) += shader.getInfoLog();
			(*r_error) += "\n";
			(*r_error) += shader.getInfoDebugLog();
		}
		return ret;
	}

	//link
	glslang::TProgram program;
	program.addShader(&shader);

	if (!program.link(messages)) {
		if (r_error) {
			(*r_error) = "Failed link:\n";
			(*r_error) += program.getInfoLog();
			(*r_error) += "\n";
			(*r_error) += program.getInfoDebugLog();
		}

		return ret;
	}

	// Auto-assigned bindings are only resolved here, not during parse.
	if (p_source_language == glslang::EShSourceHlsl && !program.mapIO()) {
		if (r_error) {
			(*r_error) = "Failed to map HLSL bindings:\n";
			(*r_error) += program.getInfoLog();
		}
		return ret;
	}

	std::vector<uint32_t> SpirV;
	spv::SpvBuildLogger logger;
	glslang::SpvOptions spvOptions;

	if (generate_spirv_debug_info) {
		spvOptions.generateDebugInfo = true;
		spvOptions.emitNonSemanticShaderDebugInfo = true;
		spvOptions.emitNonSemanticShaderDebugSource = true;
	}

	glslang::GlslangToSpv(*program.getIntermediate(stages[p_stage]), SpirV, &logger, &spvOptions);

	ret.resize(SpirV.size() * sizeof(uint32_t));
	{
		uint8_t *w = ret.ptrw();
		memcpy(w, &SpirV[0], SpirV.size() * sizeof(uint32_t));
	}

	return ret;
}

Vector<uint8_t> compile_glslang_shader(RenderingDeviceCommons::ShaderStage p_stage, const String &p_source_code, RenderingDeviceCommons::ShaderLanguageVersion p_language_version, RenderingDeviceCommons::ShaderSpirvVersion p_spirv_version, String *r_error) {
	return compile_shader(glslang::EShSourceGlsl, p_stage, p_source_code, "main", Vector<String>(), p_language_version, p_spirv_version, r_error);
}

Vector<uint8_t> compile_hlsl_shader(RenderingDeviceCommons::ShaderStage p_stage, const String &p_source_code, const String &p_entry_point, const Vector<String> &p_include_paths, RenderingDeviceCommons::ShaderLanguageVersion p_language_version, RenderingDeviceCommons::ShaderSpirvVersion p_spirv_version, String *r_error) {
	return compile_shader(glslang::EShSourceHlsl, p_stage, p_source_code, p_entry_point, p_include_paths, p_language_version, p_spirv_version, r_error);
}

void initialize_glslang_module(ModuleInitializationLevel p_level) {
	if (p_level != MODULE_INITIALIZATION_LEVEL_CORE) {
		return;
	}

	// Initialize in case it's not initialized. This is done once per thread
	// and it's safe to call multiple times.
	glslang::InitializeProcess();
}

void uninitialize_glslang_module(ModuleInitializationLevel p_level) {
	if (p_level != MODULE_INITIALIZATION_LEVEL_CORE) {
		return;
	}

	glslang::FinalizeProcess();
}
