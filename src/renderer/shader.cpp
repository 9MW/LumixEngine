#include "renderer/shader.h"
#include "engine/crc32.h"
#include "engine/file_system.h"
#include "engine/engine.h"
#include "engine/lua_wrapper.h"
#include "engine/log.h"
#include "engine/os.h"
#include "engine/path_utils.h"
#include "engine/profiler.h"
#include "engine/resource_manager.h"
#include "renderer/renderer.h"
#include "renderer/texture.h"
#include <lua.hpp>
#include <lauxlib.h>


namespace Lumix
{


const ResourceType Shader::TYPE("shader");


Shader::Shader(const Path& path, ResourceManager& resource_manager, Renderer& renderer, IAllocator& allocator)
	: Resource(path, resource_manager, allocator)
	, m_allocator(allocator)
	, m_renderer(renderer)
	, m_texture_slot_count(0)
	, m_uniforms(m_allocator)
	, m_render_states(0)
	, m_all_defines_mask(0)
	, m_render_data(nullptr)
{
}


Shader::~Shader()
{
	ASSERT(isEmpty());
}


const Shader::Program& Shader::getProgram(ShaderRenderData* rd, u32 defines)
{
	ffr::checkThread();
	auto iter = rd->programs.find(defines);
	if (!iter.isValid()) {
		PROFILE_BLOCK("compile_shader");
		static const char* shader_code_prefix = 
			R"#(#version 440
			layout (std140, binding = 0) uniform GlobalState {
				mat4 u_shadow_view_projection;
				mat4 u_shadowmap_matrices[4];
				mat4 u_camera_projection;
				mat4 u_camera_inv_projection;
				mat4 u_camera_view;
				mat4 u_camera_inv_view;
				mat4 u_camera_view_projection;
				mat4 u_camera_inv_view_projection;
				vec3 u_light_direction;
				vec3 u_light_color;
				float u_light_intensity;
				float u_light_indirect_intensity;
				float u_time;
				ivec2 u_framebuffer_size;
			};
			layout (std140, binding = 1) uniform PassState {
				mat4 u_pass_projection;
				mat4 u_pass_inv_projection;
				mat4 u_pass_view;
				mat4 u_pass_inv_view;
				mat4 u_pass_view_projection;
				mat4 u_pass_inv_view_projection;
			};
			layout (binding=14) uniform samplerCube u_irradiancemap;
			layout (binding=15) uniform samplerCube u_radiancemap;
			)#";

		const char* codes[64];
		ffr::ShaderType types[64];
		ASSERT(lengthOf(types) >= rd->sources.size());
		for (int i = 0; i < rd->sources.size(); ++i) {
			codes[i] = &rd->sources[i].code[0];
			types[i] = rd->sources[i].type;
		}
		const char* prefixes[35];
		StaticString<128> defines_code[32];
		int defines_count = 0;
		prefixes[0] = shader_code_prefix;
		prefixes[1] = rd->include.empty() ? "" : rd->include.begin();
		prefixes[2] = rd->common_source.empty() ? "" : rd->common_source.begin();
		if (defines != 0) {
			for(int i = 0; i < sizeof(defines) * 8; ++i) {
				if((defines & (1 << i)) == 0) continue;
				// TODO getShaderDefine is not threadsafe
				defines_code[defines_count] << "#define " << rd->renderer.getShaderDefine(i) << "\n";
				prefixes[3 + defines_count] = defines_code[defines_count];
				++defines_count;
			}
		}

		Program program;

		for(int& i : program.attribute_by_semantics) i = -1;
		program.handle = ffr::allocProgramHandle();
		if(program.handle.isValid() && !ffr::createProgram(program.handle, codes, types, rd->sources.size(), prefixes, 3 + defines_count, rd->path.c_str())) {
			ffr::destroy(program.handle);
			program.handle = ffr::INVALID_PROGRAM;
		}
		program.use_semantics = false;
		if (program.handle.isValid()) {
			ffr::uniformBlockBinding(program.handle, "GlobalState", 0);
			ffr::uniformBlockBinding(program.handle, "PassState", 1);
			for(const AttributeInfo& attr : rd->attributes) {
				program.use_semantics = true;
				const int loc = ffr::getAttribLocation(program.handle, attr.name);
				if(loc >= 0) {
					program.attribute_by_semantics[(int)attr.semantic] = loc;
				}
			}
		}
		rd->programs.insert(defines, program);
		iter = rd->programs.find(defines);
	}
	return iter.value();
}


static void registerCFunction(lua_State* L, const char* name, lua_CFunction function)
{
	lua_pushcfunction(L, function);
	lua_setglobal(L, name);
}


static Shader* getShader(lua_State* L)
{
	Shader* ret = nullptr;
	lua_getglobal(L, "shader");
	if (lua_type(L, -1) == LUA_TLIGHTUSERDATA)
	{
		ret = LuaWrapper::toType<Shader*>(L, -1);
	}
	lua_pop(L, 1);
	return ret;
}


static Renderer& getRendererGlobal(lua_State* L)
{
	Renderer* ret = nullptr;
	lua_getglobal(L, "renderer");
	ASSERT(lua_type(L, -1) == LUA_TLIGHTUSERDATA);
	return *LuaWrapper::toType<Renderer*>(L, -1);
}


static void uniform(lua_State* L, const char* name, const char* type)
{
	// TODO
	ASSERT(false);
	/*
	auto* shader = getShader(L);
	if (!shader) return;
	auto& u = shader->m_uniforms.emplace();
	copyString(u.name, name);
	u.name_hash = crc32(name);
	if (equalStrings(type, "float"))
	{
		u.type = Shader::Uniform::FLOAT;
		u.handle = bgfx::createUniform(name, bgfx::UniformType::Vec4);
	}
	else if (equalStrings(type, "color"))
	{
		u.type = Shader::Uniform::COLOR;
		u.handle = bgfx::createUniform(name, bgfx::UniformType::Vec4);
	}
	else if (equalStrings(type, "int"))
	{
		u.type = Shader::Uniform::INT;
		u.handle = bgfx::createUniform(name, bgfx::UniformType::Int1);
	}
	else if (equalStrings(type, "matrix4"))
	{
		u.type = Shader::Uniform::MATRIX4;
		u.handle = bgfx::createUniform(name, bgfx::UniformType::Mat4);
	}
	else if (equalStrings(type, "time"))
	{
		u.type = Shader::Uniform::TIME;
		u.handle = bgfx::createUniform(name, bgfx::UniformType::Vec4);
	}
	else if (equalStrings(type, "vec3"))
	{
		u.type = Shader::Uniform::VEC3;
		u.handle = bgfx::createUniform(name, bgfx::UniformType::Vec4);
	}
	else if (equalStrings(type, "vec4"))
	{
		u.type = Shader::Uniform::VEC4;
		u.handle = bgfx::createUniform(name, bgfx::UniformType::Vec4);
	}
	else if (equalStrings(type, "vec2"))
	{
		u.type = Shader::Uniform::VEC2;
		u.handle = bgfx::createUniform(name, bgfx::UniformType::Vec4);
	}
	else
	{
		logError("Renderer") << "Unknown uniform type " << type << " in " << shader->getPath().c_str();
	}*/
}


static void alpha_blending(lua_State* L, const char* mode)
{
	// TODO
	ASSERT(false);
	/*
	Shader* shader = getShader(L);
	if (!shader) return;
	if (equalStrings(mode, "add"))
	{
		shader->m_render_states |= BGFX_STATE_BLEND_ADD;
	}
	else if (equalStrings(mode, "alpha"))
	{
		shader->m_render_states |= BGFX_STATE_BLEND_ALPHA;
	}
	else
	{
		logError("Renderer") << "Uknown blend mode " << mode << " in " << shader->getPath().c_str();
	}*/
}


namespace LuaAPI
{


int attribute(lua_State* L)
{
	LuaWrapper::checkTableArg(L, 1);

	lua_getfield(L, LUA_GLOBALSINDEX, "this");
	Shader* shader = (Shader*)lua_touserdata(L, -1);
	lua_pop(L, 1);

	Shader::AttributeInfo& info = shader->m_render_data->attributes.emplace();
	lua_getfield(L, 1, "name");
	if (lua_isstring(L, -1)) {
		info.name = lua_tostring(L, -1);
	}
	lua_pop(L, 1);
	
	lua_getfield(L, 1, "semantic");
	if (lua_isnumber(L, -1)) {
		info.semantic = (Mesh::AttributeSemantic)lua_tointeger(L, -1);
	}
	lua_pop(L, 1);

	return 0;
}


int texture_slot(lua_State* L)
{
	LuaWrapper::checkTableArg(L, 1);

	lua_getfield(L, LUA_GLOBALSINDEX, "this");
	Shader* shader = (Shader*)lua_touserdata(L, -1);
	lua_pop(L, 1);

	if(shader->m_texture_slot_count >= lengthOf(shader->m_texture_slots)) {
		logError("Renderer") << "Too many texture slots in " << shader->getPath();
		return 0;
	}

	Shader::TextureSlot& slot = shader->m_texture_slots[shader->m_texture_slot_count];
	LuaWrapper::getOptionalStringField(L, -1, "name", slot.name, lengthOf(slot.name));

	char tmp[MAX_PATH_LENGTH];
	if(LuaWrapper::getOptionalStringField(L, -1, "default_texture", tmp, lengthOf(tmp))) {
		ResourceManagerHub& manager = shader->getResourceManager().getOwner();
		slot.default_texture = manager.load<Texture>(Path(tmp));
	}

	++shader->m_texture_slot_count;

	return 0;
}


static void source(lua_State* L, ffr::ShaderType shader_type)
{
	auto countLines = [](const char* str) {
		int count = 0;
		const char* c = str;
		while(*c) {
			if(*c == '\n') ++count;
			++c;
		}
		return count;
	};

	const char* src = LuaWrapper::checkArg<const char*>(L, 1);
	
	lua_getfield(L, LUA_GLOBALSINDEX, "this");
	Shader* shader = (Shader*)lua_touserdata(L, -1);
	lua_pop(L, 1);
	Shader::Source& srcobj = shader->m_render_data->sources.emplace(shader->m_allocator);
	srcobj.type = shader_type;

	lua_Debug ar;
	lua_getstack(L, 1, &ar);
	lua_getinfo(L, "nSl", &ar);
	const int line = ar.currentline - countLines(src);

	const StaticString<32> line_str("#line ", line, "\n");
	const int line_str_len = stringLength(line_str);
	const int src_len = stringLength(src);

	srcobj.code.resize(line_str_len + src_len + 1);
	copyMemory(&srcobj.code[0], line_str, line_str_len);
	copyMemory(&srcobj.code[line_str_len], src, src_len);
	srcobj.code.back() = '\0';
}


static int common(lua_State* L)
{
	auto countLines = [](const char* str) {
		int count = 0;
		const char* c = str;
		while(*c) {
			if(*c == '\n') ++count;
			++c;
		}
		return count;
	};

	const char* src = LuaWrapper::checkArg<const char*>(L, 1);
	
	lua_getfield(L, LUA_GLOBALSINDEX, "this");
	Shader* shader = (Shader*)lua_touserdata(L, -1);
	lua_pop(L, 1);

	lua_Debug ar;
	lua_getstack(L, 1, &ar);
	lua_getinfo(L, "nSl", &ar);
	const int line = ar.currentline - countLines(src);

	const StaticString<32> line_str("#line ", line, "\n");
	const int line_str_len = stringLength(line_str);
	const int src_len = stringLength(src);

	shader->m_render_data->common_source.resize(line_str_len + src_len + 1);
	copyMemory(&shader->m_render_data->common_source[0], line_str, line_str_len);
	copyMemory(&shader->m_render_data->common_source[line_str_len], src, src_len);
	shader->m_render_data->common_source.back() = '\0';
	return 0;
}


int vertex_shader(lua_State* L)
{
	source(L, ffr::ShaderType::VERTEX);
	return 0;
}


int fragment_shader(lua_State* L)
{
	source(L, ffr::ShaderType::FRAGMENT);
	return 0;
}


int geometry_shader(lua_State* L)
{
	source(L, ffr::ShaderType::GEOMETRY);
	return 0;
}


int include(lua_State* L)
{
	const char* path = LuaWrapper::checkArg<const char*>(L, 1);

	lua_getfield(L, LUA_GLOBALSINDEX, "this");
	Shader* shader = (Shader*)lua_touserdata(L, -1);
	lua_pop(L, 1);

	if (!shader->m_render_data->include.empty()) {
		logError("Renderer") << "More than 1 include in " << shader->getPath() << ". Max is 1.";
		return 0;
	}

	FileSystem& fs = shader->m_renderer.getEngine().getFileSystem();
	OS::InputFile file;
	if (!fs.open(path, &file)) {
		logError("Renderer") << "Failed to open include " << path << " included from " << shader->getPath();
		return 0;
	}

	shader->m_render_data->include.resize((int)file.size() + 2);
	if (!shader->m_render_data->include.empty()) {
		file.read(&shader->m_render_data->include[0], shader->m_render_data->include.size() - 1);
		shader->m_render_data->include[shader->m_render_data->include.size() - 2] = '\n';
		shader->m_render_data->include.back() = '\0';
	}

	file.close();
	return 0;
}


} // namespace LuaAPI


bool Shader::load(u64 size, const u8* mem)
{
	lua_State* L = luaL_newstate();
	luaL_openlibs(L);

	ASSERT(!m_render_data);

	IAllocator& allocator = m_renderer.getAllocator();
	m_render_data = LUMIX_NEW(allocator, ShaderRenderData)(m_renderer, allocator);
	m_render_data->path = getPath();

	lua_pushlightuserdata(L, this);
	lua_setfield(L, LUA_GLOBALSINDEX, "this");
	lua_pushcclosure(L, LuaAPI::common, 0);
	lua_setfield(L, LUA_GLOBALSINDEX, "common");
	lua_pushcclosure(L, LuaAPI::vertex_shader, 0);
	lua_setfield(L, LUA_GLOBALSINDEX, "vertex_shader");
	lua_pushcclosure(L, LuaAPI::fragment_shader, 0);
	lua_setfield(L, LUA_GLOBALSINDEX, "fragment_shader");
	lua_pushcclosure(L, LuaAPI::geometry_shader, 0);
	lua_setfield(L, LUA_GLOBALSINDEX, "geometry_shader");
	lua_pushcclosure(L, LuaAPI::include, 0);
	lua_setfield(L, LUA_GLOBALSINDEX, "include");
	lua_pushcclosure(L, LuaAPI::texture_slot, 0);
	lua_setfield(L, LUA_GLOBALSINDEX, "texture_slot");
	lua_pushcclosure(L, LuaAPI::attribute, 0);
	lua_setfield(L, LUA_GLOBALSINDEX, "attribute");

	lua_pushinteger(L, (int)Mesh::AttributeSemantic::POSITION);
	lua_setglobal(L, "SEMANTICS_POSITION");
	lua_pushinteger(L, (int)Mesh::AttributeSemantic::COLOR0);
	lua_setglobal(L, "SEMANTICS_COLOR0");
	lua_pushinteger(L, (int)Mesh::AttributeSemantic::TEXCOORD0);
	lua_setglobal(L, "SEMANTICS_TEXCOORD0");
	lua_pushinteger(L, (int)Mesh::AttributeSemantic::NORMAL);
	lua_setglobal(L, "SEMANTICS_NORMAL");
	lua_pushinteger(L, (int)Mesh::AttributeSemantic::WEIGHTS);
	lua_setglobal(L, "SEMANTICS_WEIGHTS");
	lua_pushinteger(L, (int)Mesh::AttributeSemantic::INDICES);
	lua_setglobal(L, "SEMANTICS_INDICES");
	lua_pushinteger(L, (int)Mesh::AttributeSemantic::INSTANCE0);
	lua_setglobal(L, "SEMANTICS_INSTANCE0");
	lua_pushinteger(L, (int)Mesh::AttributeSemantic::INSTANCE1);
	lua_setglobal(L, "SEMANTICS_INSTANCE1");
	lua_pushinteger(L, (int)Mesh::AttributeSemantic::INSTANCE2);
	lua_setglobal(L, "SEMANTICS_INSTANCE2");
	lua_pushinteger(L, (int)Mesh::AttributeSemantic::TANGENT);
	lua_setglobal(L, "SEMANTICS_TANGENT");
	lua_pushinteger(L, (int)Mesh::AttributeSemantic::BITANGENT);
	lua_setglobal(L, "SEMANTICS_BITANGENT");

	const StringView content((const char*)mem, (int)size);
	if (!LuaWrapper::execute(L, content, getPath().c_str(), 0)) {
		lua_close(L);
		return false;
	}

	m_size = size;
	lua_close(L);
	return true;

	// TODO
	//m_render_states = BGFX_STATE_DEPTH_TEST_GEQUAL;
}


void Shader::unload()
{
	if (m_render_data) {
		m_renderer.runInRenderThread(m_render_data, [](Renderer& renderer, void* ptr){
			ShaderRenderData* rd = (ShaderRenderData*)ptr;
			for(const Program& prg : rd->programs) {
				if (prg.handle.isValid()) ffr::destroy(prg.handle);
			}
			LUMIX_DELETE(rd->allocator, rd);
		});
		m_render_data = nullptr;
	}
		// TODO
	/*
	for (auto& uniform : m_uniforms)
	{
		bgfx::destroy(uniform.handle);
	}
	m_uniforms.clear();
	*/
	for (int i = 0; i < m_texture_slot_count; ++i)
	{
/*		if (bgfx::isValid(m_texture_slots[i].uniform_handle))
		{
			bgfx::destroy(m_texture_slots[i].uniform_handle);
		}*/
		if (m_texture_slots[i].default_texture)
		{
			Texture* t = m_texture_slots[i].default_texture;
			t->getResourceManager().unload(*t);
			m_texture_slots[i].default_texture = nullptr;
		}

		//m_texture_slots[i].uniform_handle = BGFX_INVALID_HANDLE;
	}
	m_texture_slot_count = 0;
	m_all_defines_mask = 0;
	/*
	m_instances.clear();

	m_render_states = 0;*/
}


} // namespace Lumix
