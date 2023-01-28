#include "Model.h"

#include <iostream>
#include <unordered_map>

#define TINYOBJLOADER_IMPLEMENTATION
#include <tiny_obj_loader.h>

bool GLMModel::load(const std::string& path)
{
	std::string inputfile = path;
	tinyobj::ObjReaderConfig reader_config;

	tinyobj::ObjReader reader;

	if (!reader.ParseFromFile(inputfile, reader_config)) {
		if (!reader.Error().empty()) {
			std::cerr << "TinyObjReader: " << reader.Error();
		}
		return false;
	}

	if (!reader.Warning().empty()) {
		std::cout << "TinyObjReader: " << reader.Warning();
	}

	auto& attrib = reader.GetAttrib();
	auto& shapes = reader.GetShapes();

	std::unordered_map<GLMVertex, uint32_t> uniqueVertices;

	bool hasNormal = false;

	for (const auto& shape : shapes) {
		for (const auto& index : shape.mesh.indices) {
			GLMVertex vertex = {};

			vertex.position = {
				attrib.vertices[3 * index.vertex_index + 0],
				attrib.vertices[3 * index.vertex_index + 1],
				attrib.vertices[3 * index.vertex_index + 2]
			};

			// Check if 'normal_index' is zero of positive. negative = no normal data
			if (index.normal_index >= 0) {
				tinyobj::real_t nx = attrib.normals[3 * size_t(index.normal_index) + 0];
				tinyobj::real_t ny = attrib.normals[3 * size_t(index.normal_index) + 1];
				tinyobj::real_t nz = attrib.normals[3 * size_t(index.normal_index) + 2];
				vertex.normal = { nx, ny, nz };
				hasNormal = true;
			}

			if (index.texcoord_index >= 0)
			{
				vertex.texcoord = {
				attrib.texcoords[2 * index.texcoord_index + 0],
				attrib.texcoords[2 * index.texcoord_index + 1]
				};
			}

			if (uniqueVertices.count(vertex) == 0) {
				uniqueVertices[vertex] = static_cast<uint32_t>(mesh.getVertices().size());
				mesh.addVertex(vertex);
			}

			mesh.addIndex(uniqueVertices[vertex]);
		}
	}

	if (!hasNormal)
	{
		mesh.computeNormals();
	}

	return true;
}

void GLMModel::draw() const
{
}

void GLMMesh::computeNormals()
{
	for (auto i = 0; i < indices.size(); i += 3)
	{
		auto& v0 = vertices[indices[i]];
		auto& v1 = vertices[indices[i + 1]];
		auto& v2 = vertices[indices[i + 2]];

		auto e01 = v1.position - v0.position;
		auto e02 = v2.position - v0.position;

		auto normal = glm::normalize(glm::cross(e01, e02));

		v0.normal = normal;
		v1.normal = normal;
		v2.normal = normal;
	}
}

void DXModel::convert(const GLMModel& model)
{
	for (const auto& glmVertex : model.mesh.vertices)
	{
		DXVertex vertex;
		vertex.position = { glmVertex.position.x, glmVertex.position.y, -glmVertex.position.z };
		vertex.normal = { glmVertex.normal.x, glmVertex.normal.y, -glmVertex.normal.z };
		vertex.texcoord = { glmVertex.texcoord.x, 1.0f - glmVertex.texcoord.y };
		vertex.color = { glmVertex.color.x, glmVertex.color.y, glmVertex.color.z, 1.0f };

		mesh.vertices.emplace_back(vertex);
	}

	mesh.indices = model.mesh.indices;

	mesh.vertexBufferSize = static_cast<uint32_t>(sizeof(DXVertex) * mesh.vertices.size());
	mesh.indexBufferSize = static_cast<uint32_t>(sizeof(uint32_t) * mesh.indices.size());
	mesh.vertexCount = static_cast<uint32_t>(mesh.vertices.size());
	mesh.indexCount = static_cast<uint32_t>(mesh.indices.size());
}

void DXModel::load(const std::string& path)
{
	GLMModel model;
	model.load(path);
	convert(model);
}
