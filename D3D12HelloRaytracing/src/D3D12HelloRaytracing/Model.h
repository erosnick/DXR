#pragma once

#include <string>
#include <vector>
#include <array>

#include "glm.h"

#include <DirectXMath.h>

using namespace DirectX;

struct DXVertex
{
	XMFLOAT3 position;
	XMFLOAT3 normal;
	XMFLOAT2 texcoord;
	XMFLOAT4 color;
};

struct GLMVertex
{
	GLMVertex() = default;
	GLMVertex(float x, float y, float z, float nx, float ny, float nz, float tx, float ty, float r, float g, float b)
		: position{ x, y, z }, normal(nx, ny, nz), texcoord(tx, ty), color(r, g, b)
	{}

	GLMVertex(const glm::vec3& inPosition, const glm::vec3& inNormal, const glm::vec2& inTexcoord, const glm::vec3& inColor)
	: position(inPosition), normal(inNormal), texcoord(inTexcoord), color(inColor)
	{}

	bool operator==(const GLMVertex& other) const {
		return (other.position == position) &&
			(other.normal == normal) &&
			(other.texcoord == texcoord);
	}

	glm::vec3 position{ 0.0f, 0.0f, 0.0f };
	glm::vec3 normal{ 0.0f, 0.0f, 0.0f };
	glm::vec2 texcoord{ 0.0f, 0.0f };
	glm::vec3 color{ 1.0f, 1.0f, 1.0f };
};

namespace std {
	template<> struct hash<GLMVertex> {
		size_t operator()(GLMVertex const& vertex) const {
			return ((hash<glm::vec3>()(vertex.position)
				  ^ (hash<glm::vec3>()(vertex.normal) << 1)) >> 1)
				  ^ (hash<glm::vec2>()(vertex.texcoord) << 1);
		}
	};
}

struct GLMMesh
{
	void addVertex(const GLMVertex& vertex) { vertices.emplace_back(vertex); }
	void addIndex(uint32_t index) { indices.emplace_back(index); }

	const std::vector<GLMVertex>& getVertices() const { return vertices; }
	const std::vector<uint32_t>& getIndices() const { return indices; }

	void computeNormals();

	std::vector<GLMVertex> vertices;
	std::vector<uint32_t> indices;
};

class GLMModel
{
public:
	bool load(const std::string& path);

	void draw() const;

	GLMMesh mesh;

	glm::vec3 position{ 0.0f };
	glm::vec3 scale{ 1.0f };
	glm::vec3 rotation{ 0.0f };
	glm::vec4 color{ 1.0f };
};

struct DXMesh
{
	const std::vector<DXVertex>& getVertices() const { return vertices; }
	const std::vector<uint32_t>& getIndices() const { return indices; }

	std::vector<DXVertex> vertices;
	std::vector<uint32_t> indices;

	uint32_t vertexBufferSize = 0;
	uint32_t indexBufferSize = 0;
	uint32_t vertexCount = 0;
	uint32_t indexCount = 0;
};

struct DXModel
{
	void load(const std::string& path);
	void convert(const GLMModel& model);
	DXMesh mesh;
};