#define STB_IMAGE_IMPLEMENTATION
#include "../third_party/stb_image.h"
#define TINYGLTF_NO_STB_IMAGE
#define TINYGLTF_NO_STB_IMAGE_WRITE
#define TINYGLTF_IMPLEMENTATION
#include "../third_party/tinygltf/tiny_gltf.h"
#include "../third_party/imgui/imgui.h"
#include "../third_party/imgui/backends/imgui_impl_glfw.h"
#include "../third_party/imgui/backends/imgui_impl_opengl3.h"
#include "cgmath.h"		// slee's simple math library
#include "cgut.h"		// slee's OpenGL utility
#include "trackball.h"	// virtual trackball
#include "sphere.h"		// sphere class definition

//*************************************
// global constants
static const char*	window_name = "cgbase - space";
static const char*	vert_shader_path = "../bin/shaders/trackball.vert";
static const char*	frag_shader_path = "../bin/shaders/trackball.frag";
static const char*	post_vert_shader_path = "../bin/shaders/postprocess.vert";
static const int	PANEL_WIDTH = 560;
uint				NUM_TESS = 72;		// initial tessellation factor of the sphere as a polygon

//*************************************
// common structures
struct camera
{
	vec3	eye = vec3( 0, 0, 1100 );
	vec3	at = vec3( 0, 0, 0 );
	vec3	up = vec3( 0, 1, 0 );
	mat4	view_matrix = mat4::look_at( eye, at, up );

	float	fovy = PI/4.0f; // must be in radian
	float	aspect;
	float	dnear = 1.0f;
	float	dfar = 2000.0f;
	mat4	projection_matrix;
};

//*************************************
// window objects
GLFWwindow*	window = nullptr;
ivec2		window_size = cg_default_window_size(); // initial window size
ivec2		scene_size = ivec2(std::max(window_size.x - PANEL_WIDTH, 1), window_size.y);

//*************************************
// OpenGL objects
GLuint	program = 0;		// ID holder for GPU program
GLuint	vertex_array = 0;	// ID holder for vertex array object
GLuint	gbuffer_edge_program = 0;
GLuint	postprocess_vao = 0;
GLuint	model_vertex_buffer = 0;
GLuint	model_index_buffer = 0;
GLuint	model_vertex_array = 0;
GLsizei	model_index_count = 0;
mat4	model_matrix = mat4::identity();
GLuint	scene_framebuffer = 0;
GLuint	scene_texture = 0;
GLuint	scene_normal_texture = 0;
GLuint	scene_depth_texture = 0;

struct model_part
{
	GLsizei index_count = 0;
	size_t index_offset = 0;
	GLuint texture = 0;
	GLenum draw_mode = GL_TRIANGLES;
	float base_color[4] = { 1, 1, 1, 1 };
};
std::vector<model_part> model_parts;
std::vector<GLuint> model_textures;

//*************************************
// global variables
static float FPS = 60.0f; // 60FPS
float	lastTime = (float)glfwGetTime(), timer = lastTime;
float	deltaTime = 0, nowTime = 0;
int		frame = 0;						// index of rendering frames
float	t = 0.0f;						// current simulation parameter
bool	b_rotate = true;
#ifndef GL_ES_VERSION_2_0
bool	b_wireframe = false;
#endif
auto	sphere = std::move(create_spheres());
	
std::vector<vertex> unit_sphere_vertices;	
mesh*		pMesh = nullptr;
camera		cam;
trackball	tb;

struct image_button
{
	GLuint texture = 0;
	int width = 0;
	int height = 0;
	const char* label = nullptr;
	std::vector<unsigned char> pixels;
};

image_button image_buttons[5] =
{
	{ 0, 0, 0, "Grey Weather" }, { 0, 0, 0, "Noon" },
	{ 0, 0, 0, "Sunset" }, { 0, 0, 0, "Morning Light" },
	{ 0, 0, 0, "Sunlight" }
};
int selected_image = -1;
bool cartoon_rendering = false;
bool gbuffer_linework_enabled = false;
int linework_radius = 2;
ImVec4 cluster_colors[4] =
{
	ImVec4(0.0f, 0.0f, 0.0f, 1.0f), ImVec4(0.0f, 0.0f, 0.0f, 1.0f),
	ImVec4(0.0f, 0.0f, 0.0f, 1.0f), ImVec4(0.0f, 0.0f, 0.0f, 1.0f)
};
const char* image_paths[5] =
{
	"../bin/images/Rouen Cathedral, Grey Weather (1892).png",
	"../bin/images/Rouen Cathedral, Noon (1894).png",
	"../bin/images/Rouen Cathedral, Sunset (1894).png",
	"../bin/images/Rouen Cathedral, The Portal in Morning Light (1894) (1).png",
	"../bin/images/Rouen Cathedral, West Facade, Sunlight (1894).png"
};

std::string asset_path(const char* relative_path)
{
#ifdef _MSC_VER
	module_path_t module;
	return std::string(module.drive) + module.dir + relative_path;
#else
	return std::string(relative_path);
#endif
}

void update_cluster_colors(int image_index)
{
	const image_button& source = image_buttons[image_index];
	if (source.pixels.empty()) return;

	const int pixel_count = source.width * source.height;
	const int sample_step = std::max(pixel_count / 12000, 1);
	float centers[4][3] = {};
	for (int cluster = 0; cluster < 4; ++cluster)
	{
		int pixel = std::min(cluster * pixel_count / 4, pixel_count - 1);
		const unsigned char* color = &source.pixels[pixel * 4];
		centers[cluster][0] = color[0] / 255.0f;
		centers[cluster][1] = color[1] / 255.0f;
		centers[cluster][2] = color[2] / 255.0f;
	}

	for (int iteration = 0; iteration < 12; ++iteration)
	{
		float sums[4][3] = {};
		int counts[4] = {};
		for (int pixel = 0; pixel < pixel_count; pixel += sample_step)
		{
			const unsigned char* color = &source.pixels[pixel * 4];
			int nearest = 0;
			float nearest_distance = FLT_MAX;
			for (int cluster = 0; cluster < 4; ++cluster)
			{
				float dr = color[0] / 255.0f - centers[cluster][0];
				float dg = color[1] / 255.0f - centers[cluster][1];
				float db = color[2] / 255.0f - centers[cluster][2];
				float distance = dr * dr + dg * dg + db * db;
				if (distance < nearest_distance) { nearest_distance = distance; nearest = cluster; }
			}
			sums[nearest][0] += color[0] / 255.0f;
			sums[nearest][1] += color[1] / 255.0f;
			sums[nearest][2] += color[2] / 255.0f;
			++counts[nearest];
		}
		for (int cluster = 0; cluster < 4; ++cluster)
			if (counts[cluster] > 0)
				for (int channel = 0; channel < 3; ++channel)
					centers[cluster][channel] = sums[cluster][channel] / counts[cluster];
	}

	for (int cluster = 0; cluster < 4; ++cluster)
		cluster_colors[cluster] = ImVec4(centers[cluster][0], centers[cluster][1], centers[cluster][2], 1.0f);
}

bool load_image_buttons()
{
	for (int i = 0; i < 5; ++i)
	{
		std::string path = asset_path(image_paths[i]);
		image* loaded = cg_load_image(path.c_str(), true, false);
		if (!loaded) return false;
		image_buttons[i].width = loaded->width;
		image_buttons[i].height = loaded->height;
		image_buttons[i].pixels.assign(loaded->ptr, loaded->ptr + loaded->width * loaded->height * 4);
		glGenTextures(1, &image_buttons[i].texture);
		glBindTexture(GL_TEXTURE_2D, image_buttons[i].texture);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
		glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, loaded->width, loaded->height, 0, GL_RGBA, GL_UNSIGNED_BYTE, loaded->ptr);
		delete loaded;
	}
	return true;
}

void destroy_image_buttons()
{
	for (auto& button : image_buttons)
		if (button.texture) glDeleteTextures(1, &button.texture);
}

void resize_scene_targets()
{
	if (!scene_framebuffer) glGenFramebuffers(1, &scene_framebuffer);
	if (!scene_texture) glGenTextures(1, &scene_texture);
	if (!scene_normal_texture) glGenTextures(1, &scene_normal_texture);
	if (!scene_depth_texture) glGenTextures(1, &scene_depth_texture);

	glBindTexture(GL_TEXTURE_2D, scene_texture);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
	glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, scene_size.x, scene_size.y, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);

	glBindTexture(GL_TEXTURE_2D, scene_normal_texture);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
	glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, scene_size.x, scene_size.y, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);

	glBindTexture(GL_TEXTURE_2D, scene_depth_texture);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
	glTexImage2D(GL_TEXTURE_2D, 0, GL_DEPTH_COMPONENT24, scene_size.x, scene_size.y, 0, GL_DEPTH_COMPONENT, GL_UNSIGNED_INT, nullptr);

	glBindFramebuffer(GL_FRAMEBUFFER, scene_framebuffer);
	glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, scene_texture, 0);
	glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT1, GL_TEXTURE_2D, scene_normal_texture, 0);
	glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_TEXTURE_2D, scene_depth_texture, 0);
	GLenum draw_buffers[2] = { GL_COLOR_ATTACHMENT0, GL_COLOR_ATTACHMENT1 };
	glDrawBuffers(2, draw_buffers);
	glReadBuffer(GL_COLOR_ATTACHMENT0);
	GLenum framebuffer_status = glCheckFramebufferStatus(GL_FRAMEBUFFER);
	if (framebuffer_status != GL_FRAMEBUFFER_COMPLETE)
		printf("[error] Scene framebuffer is incomplete: 0x%X\n", framebuffer_status);
	glBindFramebuffer(GL_FRAMEBUFFER, 0);
}

vec3 get_line_color()
{
	int darkest = 0;
	float darkest_value = FLT_MAX;
	for (int i = 0; i < 4; ++i)
	{
		float value = 0.299f * cluster_colors[i].x + 0.587f * cluster_colors[i].y + 0.114f * cluster_colors[i].z;
		if (value < darkest_value) { darkest_value = value; darkest = i; }
	}
	ImVec4 source = cluster_colors[darkest];
	float gray = (source.x + source.y + source.z) / 3.0f;
	vec3 line;
	line.x = std::max(0.0f, std::min(1.0f, (gray + (source.x - gray) * 2.0f) * 0.22f));
	line.y = std::max(0.0f, std::min(1.0f, (gray + (source.y - gray) * 2.0f) * 0.22f));
	line.z = std::max(0.0f, std::min(1.0f, (gray + (source.z - gray) * 2.0f) * 0.22f));
	return line;
}

bool load_gltf_image(tinygltf::Image* image, const int, std::string* error,
	std::string*, int, int, const unsigned char* bytes, int size, void*)
{
	int width = 0, height = 0, channels = 0;
	unsigned char* decoded = stbi_load_from_memory(bytes, size, &width, &height, &channels, 4);
	if (!decoded)
	{
		if (error) *error = "stb_image could not decode the GLB image";
		return false;
	}
	image->width = width;
	image->height = height;
	image->component = 4;
	image->bits = 8;
	image->pixel_type = TINYGLTF_COMPONENT_TYPE_UNSIGNED_BYTE;
	image->image.assign(decoded, decoded + width * height * 4);
	stbi_image_free(decoded);
	return true;
}

void destroy_glb_model()
{
	for (GLuint texture : model_textures)
		if (texture) glDeleteTextures(1, &texture);
	model_textures.clear();
	model_parts.clear();
	if (model_vertex_array) glDeleteVertexArrays(1, &model_vertex_array);
	if (model_vertex_buffer) glDeleteBuffers(1, &model_vertex_buffer);
	if (model_index_buffer) glDeleteBuffers(1, &model_index_buffer);
}

bool load_glb_model(const char* path)
{
	tinygltf::TinyGLTF loader;
	loader.SetImageLoader(load_gltf_image, nullptr);
	tinygltf::Model gltf;
	std::string warning, error;
	std::vector<unsigned char> glb_data;
#ifdef _MSC_VER
	int wide_length = MultiByteToWideChar(CP_ACP, 0, path, -1, nullptr, 0);
	std::wstring wide_path(wide_length, L'\0');
	MultiByteToWideChar(CP_ACP, 0, path, -1, &wide_path[0], wide_length);
	FILE* file = _wfopen(wide_path.c_str(), L"rb");
	if (file)
	{
		fseek(file, 0, SEEK_END);
		long file_size = ftell(file);
		fseek(file, 0, SEEK_SET);
		if (file_size > 0)
		{
			glb_data.resize((size_t)file_size);
			fread(glb_data.data(), 1, glb_data.size(), file);
		}
		fclose(file);
	}
#else
	mem_t binary = cg_read_binary(path);
	if (binary.ptr && binary.size > 0)
	{
		glb_data.assign((unsigned char*)binary.ptr, (unsigned char*)binary.ptr + binary.size);
		free(binary.ptr);
	}
#endif
	if (glb_data.empty() || !loader.LoadBinaryFromMemory(&gltf, &error, &warning, glb_data.data(), (unsigned int)glb_data.size()))
	{
		printf("[error] Unable to load GLB: %s\n%s\n", path, error.c_str());
		return false;
	}
	if (!warning.empty()) printf("[GLB warning] %s\n", warning.c_str());
	model_textures.resize(gltf.images.size(), 0);
	for (size_t i = 0; i < gltf.images.size(); ++i)
	{
		const tinygltf::Image& image = gltf.images[i];
		if (image.image.empty() || image.width <= 0 || image.height <= 0) continue;
		GLenum format = image.component == 4 ? GL_RGBA : GL_RGB;
		glGenTextures(1, &model_textures[i]);
		glBindTexture(GL_TEXTURE_2D, model_textures[i]);
		glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
		glTexImage2D(GL_TEXTURE_2D, 0, format, image.width, image.height, 0, format, GL_UNSIGNED_BYTE, image.image.data());
		glGenerateMipmap(GL_TEXTURE_2D);
	}

	std::vector<int> node_parent(gltf.nodes.size(), -1);
	for (size_t node_index = 0; node_index < gltf.nodes.size(); ++node_index)
		for (int child : gltf.nodes[node_index].children)
			node_parent[child] = (int)node_index;
	std::vector<mat4> node_world(gltf.nodes.size(), mat4::identity());
	std::function<mat4(int)> get_node_world = [&](int node_index)
	{
		const tinygltf::Node& node = gltf.nodes[node_index];
		mat4 local = mat4::identity();
		if (node.matrix.size() == 16)
			for (int row = 0; row < 4; ++row)
				for (int column = 0; column < 4; ++column)
					local[row * 4 + column] = (float)node.matrix[column * 4 + row];
		mat4 world = node_parent[node_index] >= 0 ? get_node_world(node_parent[node_index]) * local : local;
		node_world[node_index] = world;
		return world;
	};
	for (size_t node_index = 0; node_index < gltf.nodes.size(); ++node_index)
		get_node_world((int)node_index);
	std::vector<mat4> mesh_world(gltf.meshes.size(), mat4::identity());
	for (size_t node_index = 0; node_index < gltf.nodes.size(); ++node_index)
		if (gltf.nodes[node_index].mesh >= 0 && gltf.nodes[node_index].mesh < (int)mesh_world.size())
			mesh_world[gltf.nodes[node_index].mesh] = node_world[node_index];

	std::vector<vertex> vertices;
	std::vector<uint> indices;
	vec3 bounds_min(FLT_MAX, FLT_MAX, FLT_MAX);
	vec3 bounds_max(-FLT_MAX, -FLT_MAX, -FLT_MAX);

	for (size_t mesh_index = 0; mesh_index < gltf.meshes.size(); ++mesh_index)
	{
		const auto& mesh_data = gltf.meshes[mesh_index];
		const mat4& mesh_transform = mesh_world[mesh_index];
		for (const auto& primitive : mesh_data.primitives)
		{
			size_t primitive_index_start = indices.size();
			auto position_it = primitive.attributes.find("POSITION");
			if (position_it == primitive.attributes.end()) continue;
			const tinygltf::Accessor& position_accessor = gltf.accessors[position_it->second];
			const tinygltf::BufferView& position_view = gltf.bufferViews[position_accessor.bufferView];
			const tinygltf::Buffer& position_buffer = gltf.buffers[position_view.buffer];
			const unsigned char* position_data = position_buffer.data.data() + position_view.byteOffset + position_accessor.byteOffset;
			const size_t position_stride = position_view.byteStride ? position_view.byteStride : sizeof(float) * 3;
			int base_vertex = (int)vertices.size();

			for (size_t i = 0; i < position_accessor.count; ++i)
			{
				const float* position = reinterpret_cast<const float*>(position_data + i * position_stride);
				vertex output = {};
				vec4 transformed_position = mesh_transform * vec4(position[0], position[1], position[2], 1.0f);
				output.pos = vec3(transformed_position.x, transformed_position.y, transformed_position.z);
				output.norm = vec3(0, 0, 1);
				output.tex = vec2(0, 0);
				vertices.push_back(output);
				bounds_min.x = std::min(bounds_min.x, output.pos.x); bounds_min.y = std::min(bounds_min.y, output.pos.y); bounds_min.z = std::min(bounds_min.z, output.pos.z);
				bounds_max.x = std::max(bounds_max.x, output.pos.x); bounds_max.y = std::max(bounds_max.y, output.pos.y); bounds_max.z = std::max(bounds_max.z, output.pos.z);
			}

			auto normal_it = primitive.attributes.find("NORMAL");
			if (normal_it != primitive.attributes.end())
			{
				const tinygltf::Accessor& accessor = gltf.accessors[normal_it->second];
				const tinygltf::BufferView& view = gltf.bufferViews[accessor.bufferView];
				const tinygltf::Buffer& buffer = gltf.buffers[view.buffer];
				const unsigned char* data = buffer.data.data() + view.byteOffset + accessor.byteOffset;
				const size_t stride = view.byteStride ? view.byteStride : sizeof(float) * 3;
				for (size_t i = 0; i < accessor.count && i < position_accessor.count; ++i)
				{
					const float* normal = reinterpret_cast<const float*>(data + i * stride);
					vertices[base_vertex + i].norm = vec3(normal[0], normal[1], normal[2]);
				}
			}

			auto texcoord_it = primitive.attributes.find("TEXCOORD_0");
			if (texcoord_it != primitive.attributes.end())
			{
				const tinygltf::Accessor& accessor = gltf.accessors[texcoord_it->second];
				const tinygltf::BufferView& view = gltf.bufferViews[accessor.bufferView];
				const tinygltf::Buffer& buffer = gltf.buffers[view.buffer];
				const unsigned char* data = buffer.data.data() + view.byteOffset + accessor.byteOffset;
				const size_t stride = view.byteStride ? view.byteStride : sizeof(float) * 2;
				for (size_t i = 0; i < accessor.count && i < position_accessor.count; ++i)
				{
					const float* texcoord = reinterpret_cast<const float*>(data + i * stride);
					vertices[base_vertex + i].tex = vec2(texcoord[0], texcoord[1]);
				}
			}

			if (primitive.indices >= 0)
			{
				const tinygltf::Accessor& accessor = gltf.accessors[primitive.indices];
				const tinygltf::BufferView& view = gltf.bufferViews[accessor.bufferView];
				const tinygltf::Buffer& buffer = gltf.buffers[view.buffer];
				const unsigned char* data = buffer.data.data() + view.byteOffset + accessor.byteOffset;
				size_t component_size = accessor.componentType == TINYGLTF_COMPONENT_TYPE_UNSIGNED_BYTE ? 1 :
					accessor.componentType == TINYGLTF_COMPONENT_TYPE_UNSIGNED_SHORT ? 2 : 4;
				size_t index_stride = view.byteStride ? view.byteStride : component_size;
				for (size_t i = 0; i < accessor.count; ++i)
				{
					uint index = 0;
					const unsigned char* index_data = data + i * index_stride;
					if (accessor.componentType == TINYGLTF_COMPONENT_TYPE_UNSIGNED_BYTE) index = index_data[0];
					else if (accessor.componentType == TINYGLTF_COMPONENT_TYPE_UNSIGNED_SHORT) index = *reinterpret_cast<const unsigned short*>(index_data);
					else index = *reinterpret_cast<const unsigned int*>(index_data);
					indices.push_back((uint)base_vertex + index);
				}
			}

			if (indices.size() > primitive_index_start)
			{
				model_part part;
				part.index_offset = primitive_index_start * sizeof(uint);
				part.index_count = (GLsizei)(indices.size() - primitive_index_start);
				part.draw_mode = primitive.mode == TINYGLTF_MODE_TRIANGLE_STRIP ? GL_TRIANGLE_STRIP :
					primitive.mode == TINYGLTF_MODE_TRIANGLE_FAN ? GL_TRIANGLE_FAN : GL_TRIANGLES;
				if (primitive.material >= 0 && primitive.material < (int)gltf.materials.size())
				{
					const tinygltf::Material& material = gltf.materials[primitive.material];
					const auto& factor = material.pbrMetallicRoughness.baseColorFactor;
					for (int channel = 0; channel < 4 && channel < (int)factor.size(); ++channel)
						part.base_color[channel] = (float)factor[channel];
					int texture_index = material.pbrMetallicRoughness.baseColorTexture.index;
					if (texture_index >= 0 && texture_index < (int)gltf.textures.size())
					{
						int image_index = gltf.textures[texture_index].source;
						if (image_index >= 0 && image_index < (int)model_textures.size())
							part.texture = model_textures[image_index];
					}
				}
				model_parts.push_back(part);
			}
		}
	}

	if (vertices.empty() || indices.empty()) return false;
	vec3 center = (bounds_min + bounds_max) * 0.5f;
	vec3 extent = bounds_max - bounds_min;
	float largest_extent = std::max(extent.x, std::max(extent.y, extent.z));
	float scale = largest_extent > 0.0f ? 700.0f / largest_extent : 1.0f;
	model_matrix = mat4::scale(scale, scale, scale) * mat4::translate(-center);

	glGenBuffers(1, &model_vertex_buffer);
	glBindBuffer(GL_ARRAY_BUFFER, model_vertex_buffer);
	glBufferData(GL_ARRAY_BUFFER, sizeof(vertex) * vertices.size(), vertices.data(), GL_STATIC_DRAW);
	glGenBuffers(1, &model_index_buffer);
	glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, model_index_buffer);
	glBufferData(GL_ELEMENT_ARRAY_BUFFER, sizeof(uint) * indices.size(), indices.data(), GL_STATIC_DRAW);
	model_vertex_array = cg_create_vertex_array(model_vertex_buffer, model_index_buffer);
	model_index_count = (GLsizei)indices.size();
	return model_vertex_array != 0;
}

//*************************************
void update()
{
	if (b_rotate) t += 0.002f / PI;

	// update projection matrix
	cam.aspect = scene_size.x/float(scene_size.y);
	cam.projection_matrix = mat4::perspective( cam.fovy, cam.aspect, cam.dnear, cam.dfar );

	// update uniform variables in vertex/fragment shaders
	glUseProgram(program);
	GLint uloc;
	uloc = glGetUniformLocation( program, "view_matrix" );			if(uloc>-1) glUniformMatrix4fv( uloc, 1, GL_TRUE, cam.view_matrix );
	uloc = glGetUniformLocation( program, "projection_matrix" );	if(uloc>-1) glUniformMatrix4fv( uloc, 1, GL_TRUE, cam.projection_matrix );
}

void update_vertex_buffer( const std::vector<vertex>& vertices, uint N );
std::vector<vertex> create_sphere_vertices(uint N);

void draw_ui()
{
	ImGui::SetNextWindowPos(ImVec2(0, 0), ImGuiCond_Always);
	ImGui::SetNextWindowSize(ImVec2((float)PANEL_WIDTH, (float)window_size.y), ImGuiCond_Always);
	ImGuiWindowFlags flags = ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize |
		ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoSavedSettings;
	ImGui::Begin("Scene Controls", nullptr, flags);
	for (int i = 0; i < 5; ++i)
	{
		float aspect = image_buttons[i].width > 0 ? (float)image_buttons[i].width / image_buttons[i].height : 1.0f;
		ImVec2 size(180.0f, 180.0f / aspect);
		if (ImGui::ImageButton(image_buttons[i].label, (ImTextureID)(intptr_t)image_buttons[i].texture, size))
		{
			selected_image = i;
			update_cluster_colors(selected_image);
			cartoon_rendering = true;
		}
		ImGui::SameLine();
		ImGui::Text("%s%s", image_buttons[i].label, selected_image == i ? "  [selected]" : "");
	}
	ImGui::Spacing();
	ImGui::Checkbox("G-buffer Linework", &gbuffer_linework_enabled);
	ImGui::SliderInt("Line Width (px)", &linework_radius, 1, 4);
	if (ImGui::Button("Reset Camera", ImVec2(-1, 0)))
	{
		cam = camera();
		selected_image = -1;
		cartoon_rendering = false;
		gbuffer_linework_enabled = false;
		linework_radius = 2;
		for (ImVec4& color : cluster_colors)
			color = ImVec4(0.0f, 0.0f, 0.0f, 1.0f);
	}
	ImGui::End();
}

void render()
{
	ImGui_ImplOpenGL3_NewFrame();
	ImGui_ImplGlfw_NewFrame();
	ImGui::NewFrame();
	draw_ui();

	glDisable(GL_BLEND);
	glEnable(GL_DEPTH_TEST);
	glDepthMask(GL_TRUE);
	glBindFramebuffer(GL_FRAMEBUFFER, scene_framebuffer);
	glViewport(0, 0, scene_size.x, scene_size.y);
	glClear( GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT );

	// notify GL that we use our own program
	glUseProgram( program );

	GLint model_location = glGetUniformLocation(program, "model_matrix");
	glBindVertexArray(model_vertex_array);
	glActiveTexture(GL_TEXTURE0);
	glUniform1i(glGetUniformLocation(program, "albedo_texture"), 0);
	glUniform4fv(glGetUniformLocation(program, "palette_colors"), 4, &cluster_colors[0].x);
	glUniformMatrix4fv(model_location, 1, GL_TRUE, model_matrix);
	glUniform1i(glGetUniformLocation(program, "toon_enabled"), cartoon_rendering ? GL_TRUE : GL_FALSE);
	if (cartoon_rendering)
	{
		glUniform1f(glGetUniformLocation(program, "outline_scale"), 1.008f);
		glUniform1i(glGetUniformLocation(program, "outline_pass"), GL_TRUE);
		glEnable(GL_CULL_FACE);
		glCullFace(GL_FRONT);
		glDepthMask(GL_FALSE);
		for (const model_part& part : model_parts)
			glDrawElements(part.draw_mode, part.index_count, GL_UNSIGNED_INT, (const void*)part.index_offset);
		glDepthMask(GL_TRUE);
		glDisable(GL_CULL_FACE);
	}
	glUniform1f(glGetUniformLocation(program, "outline_scale"), 1.0f);
	glUniform1i(glGetUniformLocation(program, "outline_pass"), GL_FALSE);
	for (const model_part& part : model_parts)
	{
		glBindTexture(GL_TEXTURE_2D, part.texture);
		glUniform1i(glGetUniformLocation(program, "has_texture"), part.texture != 0 ? GL_TRUE : GL_FALSE);
		glUniform4fv(glGetUniformLocation(program, "base_color"), 1, part.base_color);
		glDrawElements(part.draw_mode, part.index_count, GL_UNSIGNED_INT, (const void*)part.index_offset);
	}
	glDisable(GL_DEPTH_TEST);
	glBindVertexArray(postprocess_vao);

	// Detect edges from the G-buffer and composite them over the original scene.
	glUseProgram(gbuffer_edge_program);
	glActiveTexture(GL_TEXTURE0);
	glBindTexture(GL_TEXTURE_2D, scene_texture);
	glUniform1i(glGetUniformLocation(gbuffer_edge_program, "scene_texture"), 0);
	glActiveTexture(GL_TEXTURE1);
	glBindTexture(GL_TEXTURE_2D, scene_normal_texture);
	glUniform1i(glGetUniformLocation(gbuffer_edge_program, "normal_texture"), 1);
	glActiveTexture(GL_TEXTURE2);
	glBindTexture(GL_TEXTURE_2D, scene_depth_texture);
	glUniform1i(glGetUniformLocation(gbuffer_edge_program, "depth_texture"), 2);
	glUniform2f(glGetUniformLocation(gbuffer_edge_program, "texel_size"), 1.0f / scene_size.x, 1.0f / scene_size.y);
	glUniform1i(glGetUniformLocation(gbuffer_edge_program, "line_radius"), linework_radius);
	vec3 line_color = get_line_color();
	glUniform3f(glGetUniformLocation(gbuffer_edge_program, "line_color"), line_color.x, line_color.y, line_color.z);
	glUniform1i(glGetUniformLocation(gbuffer_edge_program, "linework_enabled"), gbuffer_linework_enabled ? GL_TRUE : GL_FALSE);
	glBindFramebuffer(GL_FRAMEBUFFER, 0);
	glViewport(PANEL_WIDTH, 0, scene_size.x, scene_size.y);
	glDrawArrays(GL_TRIANGLES, 0, 3);
	glEnable(GL_DEPTH_TEST);

	ImDrawList* overlay = ImGui::GetForegroundDrawList();
	const float swatch_size = 56.0f;
	const float swatch_gap = 12.0f;
	const float swatch_x = PANEL_WIDTH + 24.0f;
	const float swatch_y = 24.0f;
	for (int cluster = 0; cluster < 4; ++cluster)
	{
		float x = swatch_x + cluster * (swatch_size + swatch_gap);
		ImU32 color = ImGui::ColorConvertFloat4ToU32(cluster_colors[cluster]);
		overlay->AddRectFilled(ImVec2(x, swatch_y), ImVec2(x + swatch_size, swatch_y + swatch_size), color, 4.0f);
		overlay->AddRect(ImVec2(x, swatch_y), ImVec2(x + swatch_size, swatch_y + swatch_size), IM_COL32(255, 255, 255, 180), 4.0f, 0, 2.0f);
	}

	ImGui::Render();
	ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());

	// swap front and back buffers, and display to screen
	glfwSwapBuffers( window );
}

void reshape( GLFWwindow* window, int width, int height )
{
	// set current viewport in pixels (win_x, win_y, win_width, win_height)
	// viewport: the window area that are affected by rendering 
	window_size = ivec2(width,height);
	scene_size = ivec2(std::max(width - PANEL_WIDTH, 1), std::max(height, 1));
	glViewport( PANEL_WIDTH, 0, scene_size.x, scene_size.y );
	if (scene_framebuffer)
	{
		resize_scene_targets();
	}
}

void print_help()
{
	printf( "[help]\n" );
	printf( "- press ESC or 'q' to terminate the program\n" );
	printf( "- press F1 or 'h' to see help\n" );
#ifndef GL_ES_VERSION_2_0
	printf("- press 'w' to toggle wireframe\n");
#endif
	printf( "- press Home to reset camera\n" );
	printf( "- press Pause to pause the simulation\n");
	printf( "\n" );
}

std::vector<vertex> create_sphere_vertices(uint N)
{
	std::vector<vertex> v;
	for (uint lat = 0; lat <= N / 2; lat++) {
		for (uint lon = 0; lon <= N; lon++) {
			float t1 = PI * 2.0f * lon / float(N);
			float t2 = PI * lat / float(N / 2);
			v.push_back({ vec3(sin(t2) * cos(t1), sin(t2) * sin(t1),cos(t2)),
						  vec3(sin(t2) * cos(t1), sin(t2) * sin(t1),cos(t2)),
						  vec2(t1 / (2 * PI), 1 - t2 / PI) });
		}
	}
	return v;
}

void update_vertex_buffer( const std::vector<vertex>& vertices, uint N )
{
	static GLuint vertex_buffer = 0;	// ID holder for vertex buffer
	static GLuint index_buffer = 0;		// ID holder for index buffer

	// clear and create new buffers
	if(vertex_buffer)	glDeleteBuffers( 1, &vertex_buffer );	vertex_buffer = 0;
	if(index_buffer)	glDeleteBuffers( 1, &index_buffer );	index_buffer = 0;

	// check exceptions
	if(vertices.empty()){ printf("[error] vertices is empty.\n"); return; }

	// create index buffers
	std::vector<uint> indices;
	uint k1, k2;
	for (uint i = 0; i < N/2; i++) // latitude
	{
		k1 = i * (N + 1);
		k2 = (i + 1) * (N + 1);
		for (uint j = 0; j < N; j++) // longitude
		{
			if (i > 0) { // not for top one
				indices.push_back(k1);
				indices.push_back(k2);
				indices.push_back(k1 + 1);
			}
			if (i < (N / 2 - 1)) { // not for bottom one
				indices.push_back(k1 + 1);
				indices.push_back(k2);
				indices.push_back(k2 + 1);
			}
			
			k1++;
			k2++;
		}

		// generation of vertex buffer: use vertices as it is
		glGenBuffers( 1, &vertex_buffer );
		glBindBuffer( GL_ARRAY_BUFFER, vertex_buffer );
		glBufferData( GL_ARRAY_BUFFER, sizeof(vertex)*vertices.size(), &vertices[0], GL_STATIC_DRAW);

		// geneation of index buffer
		glGenBuffers( 1, &index_buffer );
		glBindBuffer( GL_ELEMENT_ARRAY_BUFFER, index_buffer );
		glBufferData( GL_ELEMENT_ARRAY_BUFFER, sizeof(uint)*indices.size(), &indices[0], GL_STATIC_DRAW );
	}

	// generate vertex array object, which is mandatory for OpenGL 3.3 and higher
	if(vertex_array) glDeleteVertexArrays(1,&vertex_array);
	vertex_array = cg_create_vertex_array( vertex_buffer, index_buffer );
	if(!vertex_array){ printf("%s(): failed to create vertex aray\n",__func__); return; }
}

void keyboard( GLFWwindow* window, int key, int scancode, int action, int mods )
{
	if (ImGui::GetIO().WantCaptureKeyboard) return;
	if(action==GLFW_PRESS)
	{
		if(key==GLFW_KEY_ESCAPE||key==GLFW_KEY_Q)	glfwSetWindowShouldClose( window, GL_TRUE );
		else if(key==GLFW_KEY_H||key==GLFW_KEY_F1)	print_help();
#ifndef GL_ES_VERSION_2_0
		else if (key == GLFW_KEY_W)
		{
			b_wireframe = !b_wireframe;
			glPolygonMode(GL_FRONT_AND_BACK, b_wireframe ? GL_LINE : GL_FILL);
			printf("> using %s mode\n", b_wireframe ? "wireframe" : "solid");
		}
#endif
		else if(key==GLFW_KEY_HOME)					cam=camera();
		else if (key == GLFW_KEY_R)					b_rotate = !b_rotate;
	}
}

void mouse( GLFWwindow* window, int button, int action, int mods )
{
	if (ImGui::GetIO().WantCaptureMouse) return;
	tb.button = button;
	tb.mods = mods;
	dvec2 pos; glfwGetCursorPos(window, &pos.x, &pos.y);
	if (pos.x < PANEL_WIDTH) return;
	vec2 npos = cursor_to_ndc(dvec2(pos.x - PANEL_WIDTH, pos.y), scene_size);
	if (action == GLFW_PRESS)			tb.begin(cam.view_matrix, npos);
	else if (action == GLFW_RELEASE)	tb.end();

}

void motion( GLFWwindow* window, double x, double y )
{
	if (ImGui::GetIO().WantCaptureMouse) return;
	if(!tb.is_tracking()) return;
	if (x < PANEL_WIDTH) return;
	vec2 npos = cursor_to_ndc( dvec2(x - PANEL_WIDTH,y), scene_size );
	// get n axis
	vec3 n = vec3(cam.eye - cam.at).normalize();
	vec3 u = cam.up.cross(n).normalize();
	vec3 v = n.cross(u).normalize();
	// get uv plane
	vec3 uv = cross(u, v).normalize();;

	if (tb.button == GLFW_MOUSE_BUTTON_LEFT && tb.mods== 0)
	{
		float distance_to_target = (cam.eye - cam.at).length();
		cam.view_matrix = tb.update(npos);
		vec3 view_axis = vec3(cam.view_matrix._31, cam.view_matrix._32, cam.view_matrix._33).normalize();
		cam.eye = cam.at + view_axis * distance_to_target;
	}
	else if (tb.button == GLFW_MOUSE_BUTTON_MIDDLE || (tb.button == GLFW_MOUSE_BUTTON_LEFT && (tb.mods & GLFW_MOD_CONTROL))) {
		cam.eye += tb.update_pan(npos, uv);
		cam.at += tb.update_pan(npos, uv);
		cam.view_matrix = mat4::look_at(cam.eye, cam.at, cam.up);
	}
	else if (tb.button == GLFW_MOUSE_BUTTON_RIGHT || (tb.button == GLFW_MOUSE_BUTTON_LEFT && (tb.mods & GLFW_MOD_SHIFT))) {
		cam.eye = tb.update_zoom(npos, cam.eye, n);
		cam.view_matrix = mat4::look_at(cam.eye, cam.at, cam.up);
	}
}

void scroll( GLFWwindow* window, double xoffset, double yoffset )
{
	if (ImGui::GetIO().WantCaptureMouse) return;
	vec3 view_direction = (cam.eye - cam.at).normalize();
	float distance_to_target = (cam.eye - cam.at).length();
	distance_to_target *= powf(0.85f, (float)yoffset);
	distance_to_target = std::max(80.0f, std::min(distance_to_target, 5000.0f));
	cam.eye = cam.at + view_direction * distance_to_target;
	cam.view_matrix = mat4::look_at(cam.eye, cam.at, cam.up);
}

bool user_init()
{
	// log hotkeys
	print_help();

	// init GL states
	glLineWidth(1.0f);
	glClearColor( 39/255.0f, 40/255.0f, 34/255.0f, 1.0f );	// set clear color
	glDisable( GL_CULL_FACE );								// GLB models may use a different winding convention
	glEnable( GL_DEPTH_TEST );								// turn on depth tests

	// define the position of four corner vertices
	unit_sphere_vertices = std::move(create_sphere_vertices(NUM_TESS));

	// create vertex buffer; called again when index buffering mode is toggled
	update_vertex_buffer(unit_sphere_vertices, NUM_TESS);

	return true;
}

void user_finalize()
{
}

int main( int argc, char* argv[] )
{
	// create window and initialize OpenGL extensions
	if(!(window = cg_create_window( window_name, window_size.x, window_size.y ))){ glfwTerminate(); return 1; }
	if(!cg_init_extensions( window )){ glfwTerminate(); return 1; }	// version and extensions

	// initializations and validations
	if(!(program=cg_create_program( vert_shader_path, frag_shader_path ))){ glfwTerminate(); return 1; }	// create and compile shaders/program
	if(!(gbuffer_edge_program=cg_create_program( post_vert_shader_path, "../bin/shaders/gbuffer_edge.frag" ))){ glfwTerminate(); return 1; }
	glGenVertexArrays(1, &postprocess_vao);
	if(!user_init()){ printf( "Failed to user_init()\n" ); glfwTerminate(); return 1; }					// user initialization
	resize_scene_targets();
	std::string model_path = asset_path("models/cologne_cathedral.glb");
	if(!load_glb_model(model_path.c_str())){ printf( "Failed to load GLB model\n" ); glfwTerminate(); return 1; }

	if(!load_image_buttons()){ printf( "Failed to load image buttons\n" ); glfwTerminate(); return 1; }

	// register event callbacks
	glfwSetWindowSizeCallback( window, reshape );	// callback for window resizing events
    glfwSetKeyCallback( window, keyboard );			// callback for keyboard events
	glfwSetMouseButtonCallback( window, mouse );	// callback for mouse click inputs
	glfwSetCursorPosCallback( window, motion );		// callback for mouse movement
	glfwSetScrollCallback( window, scroll );			// callback for mouse wheel zoom

	IMGUI_CHECKVERSION();
	ImGui::CreateContext();
	ImGui::GetIO().FontGlobalScale = 2.0f;
	ImGui::StyleColorsDark();
	ImGui_ImplGlfw_InitForOpenGL(window, true);
	ImGui_ImplOpenGL3_Init("#version 330");

	// enters rendering/event loop
	for( frame=0; !glfwWindowShouldClose(window); frame++ )
	{
		nowTime = (float)glfwGetTime();
		deltaTime += (nowTime - lastTime) * FPS;
		lastTime = nowTime;

		while (deltaTime >= 1.0f) {
			glfwPollEvents();	// polling and processing of events
			update();			// per-frame update
			render();			// per-frame render
			deltaTime -= 1.0f;
		}
	}

	// normal termination
	user_finalize();
	ImGui_ImplOpenGL3_Shutdown();
	ImGui_ImplGlfw_Shutdown();
	ImGui::DestroyContext();
	destroy_image_buttons();
	destroy_glb_model();
	if (scene_texture) glDeleteTextures(1, &scene_texture);
	if (scene_framebuffer) glDeleteFramebuffers(1, &scene_framebuffer);
	if (scene_normal_texture) glDeleteTextures(1, &scene_normal_texture);
	if (scene_depth_texture) glDeleteTextures(1, &scene_depth_texture);
	if (postprocess_vao) glDeleteVertexArrays(1, &postprocess_vao);
	cg_destroy_window(window);

	return 0;
}
