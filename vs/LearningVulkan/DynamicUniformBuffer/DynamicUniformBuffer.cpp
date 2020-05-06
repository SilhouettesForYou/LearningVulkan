#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <vector>
#include <array>
#include <random>

#define GLM_FORCE_RADIANS
#define GLM_FORCE_DEPTH_ZERO_TO_ONE
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

#include <vulkan/vulkan.h>
#include "VulkanBase.h"
#include "VulkanDevice.hpp"
#include "VulkanBuffer.hpp"
#include <corecrt_math_defines.h>

#define VERTEX_BUFFER_BIND_ID 0
#define ENABLE_VALIDATION false
#define OBJECT_INSTANCES 125

// Vertex layout for this example
struct Vertex
{
	float pos[3];
	float color[3];
};

// Wrapper functions for aligned memory allocation
// There is currently no standard for this C++ that works across all platform and verdors
void* AlignedAlloc(size_t size, size_t alignment)
{
	void* data = nullptr;
#if defined(_MSC_VER) || defined(__MINGW32__)
	data = _aligned_malloc(size, alignment);
#else
	int res = posix_memalign(&data, aligment, size);
	if (res != 0)
		data = nullptr;
#endif
	return data;
}

void AlignedFree(void* data)
{
#if defined(_MSC_VER) || defined(__MINGW32__)
	_aligned_free(data);
#else
	free(data);
#endif
}

class VulkanExampleDynamicUniformBuffer : public VulkanBase
{
public:
	struct
	{
		VkPipelineVertexInputStateCreateInfo inputState;
		std::vector<VkVertexInputBindingDescription> bindingDescriptions;
		std::vector<VkVertexInputAttributeDescription> attributeDescriptions;
	} vertices;

	vks::Buffer vertexBuffer;
	vks::Buffer indexBuffer;
	uint32_t indexCount;

	struct 
	{
		vks::Buffer view;
		vks::Buffer dynamic;
	} uniformBuffers;

	struct
	{
		glm::mat4 projection;
		glm::mat4 view;
	} uboVS;

	// Store random per-object rotations
	glm::vec3 rotations[OBJECT_INSTANCES];
	glm::vec3 rotationSpeeds[OBJECT_INSTANCES];

	// One big uniform buffer that contains all matrices
	// Note that we need to manually allocate the data to cope for GPU-specific uniform buffer offset alignments
	struct
	{
		glm::mat4* model = nullptr;
	} uboDataDynamic;

	VkPipeline pipeline;
	VkPipelineLayout pipelineLayout;
	VkDescriptorSet descriptorSet;
	VkDescriptorSetLayout descriptorSetLayout;

	float animationTimer{ 0.0f };
	size_t dynamicAlignment;

	VulkanExampleDynamicUniformBuffer() : VulkanBase(ENABLE_VALIDATION)
	{
		title = "Dynamic uniform buffers";
		camera.type = Camera::CameraType::lookat;
		camera.SetPosition(glm::vec3(0.0f, 0.0f, -30.0f));
		camera.SetRotation(glm::vec3(0.0f));
		camera.SetPerspective(60.0f, static_cast<float>(width) / height, 0.1f, 256.0f);
		settings.overlay = true;
	}

	~VulkanExampleDynamicUniformBuffer()
	{
		if (uboDataDynamic.model)
			AlignedFree(uboDataDynamic.model);

		// Clean up used Vulkan resources
		vkDestroyPipeline(device, pipeline, nullptr);

		vkDestroyPipelineLayout(device, pipelineLayout, nullptr);
		vkDestroyDescriptorSetLayout(device, descriptorSetLayout, nullptr);

		uniformBuffers.view.Destroy();
		uniformBuffers.dynamic.Destroy();
	}

	void BuildCommandBuffers()
	{
		VkCommandBufferBeginInfo cmdBufInfo = vks::initializers::CommandBufferBeginInfo();

		VkClearValue clearValues[2];
		clearValues[0].color = defaultClearColor;
		clearValues[1].depthStencil = { 1.0f, 0 };

		VkRenderPassBeginInfo renderPassBeginInfo = vks::initializers::RenderPassBeginInfo();
		renderPassBeginInfo.renderPass = renderPass;
		renderPassBeginInfo.renderArea.offset.x = 0;
		renderPassBeginInfo.renderArea.offset.y = 0;
		renderPassBeginInfo.renderArea.extent.width = width;
		renderPassBeginInfo.renderArea.extent.height = height;
		renderPassBeginInfo.clearValueCount = 2;
		renderPassBeginInfo.pClearValues = clearValues;

		for (size_t i = 0; i < drawCmdBuffers.size(); i++)
		{
			renderPassBeginInfo.framebuffer = frameBuffers[i];
			VK_CHECK_RESULT(vkBeginCommandBuffer(drawCmdBuffers[i], &cmdBufInfo));

			vkCmdBeginRenderPass(drawCmdBuffers[i], &renderPassBeginInfo, VK_SUBPASS_CONTENTS_INLINE);
			
			VkViewport viewport = vks::initializers::Viewport(static_cast<float>(width), static_cast<float>(height), 0.0f, 1.0f);
			vkCmdSetViewport(drawCmdBuffers[i], 0, 1, &viewport);

			VkRect2D scissor = vks::initializers::Rect2D(width, height, 0, 0);
			vkCmdSetScissor(drawCmdBuffers[i], 0, 1, &scissor);

			vkCmdBindPipeline(drawCmdBuffers[i], VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);

			VkDeviceSize offsets[1] = { 0 };
			vkCmdBindVertexBuffers(drawCmdBuffers[i], VERTEX_BUFFER_BIND_ID, 1, &vertexBuffer.buffer, offsets);
			vkCmdBindIndexBuffer(drawCmdBuffers[i], indexBuffer.buffer, 0, VK_INDEX_TYPE_UINT32);

			// Render multiple objects using different model matrices by dynamically offsetting into one uniform buffer
			for (uint32_t j = 0; j < OBJECT_INSTANCES; j++)
			{
				// One dynamic offset per dynamic descriptor to offset into the ubo containing all model matrices
				uint32_t dynamicOffset = j * static_cast<uint32_t>(dynamicAlignment);
				// Bind the descriptor set for rendering a mesh using the dynamic offset
				vkCmdBindDescriptorSets(drawCmdBuffers[i], VK_PIPELINE_BIND_POINT_GRAPHICS, pipelineLayout, 0, 1, &descriptorSet, 1, &dynamicOffset);

				vkCmdDrawIndexed(drawCmdBuffers[i], indexCount, 1, 0, 0, 0);
			}

			DrawUI(drawCmdBuffers[i]);

			vkCmdEndRenderPass(drawCmdBuffers[i]);

			VK_CHECK_RESULT(vkEndCommandBuffer(drawCmdBuffers[i]));
		}

	}

	void Draw()
	{
		VulkanBase::PrepareFrame();
		submitInfo.commandBufferCount = 1;
		submitInfo.pCommandBuffers = &drawCmdBuffers[currentBuffer];
		VK_CHECK_RESULT(vkQueueSubmit(queue, 1, &submitInfo, VK_NULL_HANDLE));
		VulkanBase::SubmitFrame();
	}

	void GenerateCube()
	{
		std::vector<Vertex> vertices =
		{
			{ { -1.0f, -1.0f,  1.0f },{ 1.0f, 0.0f, 0.0f } },
			{ {  1.0f, -1.0f,  1.0f },{ 0.0f, 1.0f, 0.0f } },
			{ {  1.0f,  1.0f,  1.0f },{ 0.0f, 0.0f, 1.0f } },
			{ { -1.0f,  1.0f,  1.0f },{ 0.0f, 0.0f, 0.0f } },
			{ { -1.0f, -1.0f, -1.0f },{ 1.0f, 0.0f, 0.0f } },
			{ {  1.0f, -1.0f, -1.0f },{ 0.0f, 1.0f, 0.0f } },
			{ {  1.0f,  1.0f, -1.0f },{ 0.0f, 0.0f, 1.0f } },
			{ { -1.0f,  1.0f, -1.0f },{ 0.0f, 0.0f, 0.0f } }
		};

		std::vector<uint32_t> indices = 
		{
			0,1,2, 2,3,0, 1,5,6, 6,2,1, 7,6,5, 5,4,7, 4,0,3, 3,7,4, 4,5,1, 1,0,4, 3,2,6, 6,7,3,
		};

		indexCount = static_cast<uint32_t>(indices.size());

		// Vertex buffers
		VK_CHECK_RESULT(vulkanDevice->CreateBuffer(VK_BUFFER_USAGE_VERTEX_BUFFER_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, &vertexBuffer, vertices.size() * sizeof(Vertex), vertices.data()));
		// Index buffers
		VK_CHECK_RESULT(vulkanDevice->CreateBuffer(VK_BUFFER_USAGE_INDEX_BUFFER_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, &indexBuffer, indices.size() * sizeof(uint32_t), indices.data()));
	}

	void SetupVertexDescriptions()
	{
		// Binding description
		vertices.bindingDescriptions =
		{
			vks::initializers::VertexInputBindingDescription(VERTEX_BUFFER_BIND_ID, sizeof(Vertex), VK_VERTEX_INPUT_RATE_VERTEX)
		};

		// Attribute descriptions
		vertices.attributeDescriptions =
		{
			vks::initializers::VertexInputAttributeDescription(VERTEX_BUFFER_BIND_ID, 0, VK_FORMAT_R32G32B32_SFLOAT, offsetof(Vertex, pos)),
			vks::initializers::VertexInputAttributeDescription(VERTEX_BUFFER_BIND_ID, 0, VK_FORMAT_R32G32B32_SFLOAT, offsetof(Vertex, color))
		};

		vertices.inputState = vks::initializers::PipelineVertexInputStateCreateInfo();
		vertices.inputState.vertexBindingDescriptionCount = static_cast<uint32_t>(vertices.bindingDescriptions.size());
		vertices.inputState.pVertexBindingDescriptions = vertices.bindingDescriptions.data();
		vertices.inputState.vertexAttributeDescriptionCount = static_cast<uint32_t>(vertices.attributeDescriptions.size());
		vertices.inputState.pVertexAttributeDescriptions = vertices.attributeDescriptions.data();
	}

	void SetupDescriptorPool()
	{
		std::vector <VkDescriptorPoolSize> poolSize = 
		{ 
			vks::initializers::DescriptorPoolSize(VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1),
			vks::initializers::DescriptorPoolSize(VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC, 1),
			vks::initializers::DescriptorPoolSize(VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1)
		};
		VkDescriptorPoolCreateInfo descriptorPoolInfo =
			vks::initializers::DescriptorPoolCreateInfo(poolSize.size(), poolSize.data(), 2);
		VK_CHECK_RESULT(vkCreateDescriptorPool(device, &descriptorPoolInfo, nullptr, &descriptorPool));
	}

	void  SetupDesctiptorSetLayout()
	{
		std::vector<VkDescriptorSetLayoutBinding> setLayoutBindings =
		{
			// Binding 0 : Vertex shader uniform buffer
			vks::initializers::DescriptorSetLayoutBinding(VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, VK_SHADER_STAGE_VERTEX_BIT, 0),
			vks::initializers::DescriptorSetLayoutBinding(VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC, VK_SHADER_STAGE_VERTEX_BIT, 1),
			vks::initializers::DescriptorSetLayoutBinding(VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_FRAGMENT_BIT, 2)
		};

		VkDescriptorSetLayoutCreateInfo descriptorLayout =
			vks::initializers::DescriptorSetLayoutCreateInfo(setLayoutBindings.data(), setLayoutBindings.size());

		VK_CHECK_RESULT(vkCreateDescriptorSetLayout(device, &descriptorLayout, nullptr, &descriptorSetLayout));

		VkPipelineLayoutCreateInfo pPipelineLayoutCreateInfo =
			vks::initializers::PipelineLayoutCreateInfo(&descriptorSetLayout, 1);

		VK_CHECK_RESULT(vkCreatePipelineLayout(device, &pPipelineLayoutCreateInfo, nullptr, &pipelineLayout));
	}

	void SetupDesctiptorSet()
	{
		VkDescriptorSetAllocateInfo allocInfo =
			vks::initializers::DescriptorSetAllocateInfo(descriptorPool, &descriptorSetLayout, 1);
		VK_CHECK_RESULT(vkAllocateDescriptorSets(device, &allocInfo, &descriptorSet));

		std::vector<VkWriteDescriptorSet> writeDescriptorSets =
		{
			// Binding 0 : Projection/View matrix uniform buffer			
			vks::initializers::WriteDescriptorSet(descriptorSet, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 0, &uniformBuffers.view.descriptor),
			// Binding 1 : Instance matrix as dynamic uniform buffer
			vks::initializers::WriteDescriptorSet(descriptorSet, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC, 1, &uniformBuffers.dynamic.descriptor),
		};

		vkUpdateDescriptorSets(device, writeDescriptorSets.size(), writeDescriptorSets.data(), 0, NULL);
	}

	void PreparePipelines()
	{
		VkPipelineInputAssemblyStateCreateInfo inputAssemblyState = vks::initializers::PipelineInputAssemblyStateCreateInfo(VK_PRIMITIVE_TOPOLOGY_LINE_LIST, 0, VK_FALSE);
		VkPipelineRasterizationStateCreateInfo rasterizationState = vks::initializers::PipelineRasterizationStateCreateInfo(VK_POLYGON_MODE_FILL, VK_CULL_MODE_BACK_BIT, VK_FRONT_FACE_CLOCKWISE, 0);
		VkPipelineColorBlendAttachmentState blendAttachmentState = vks::initializers::PipelineColorBlendAttachmentState(0xf, VK_FALSE);
		VkPipelineColorBlendStateCreateInfo colorBlendState = vks::initializers::PipelineColorBlendStateCreateInfo(1, &blendAttachmentState);
		VkPipelineDepthStencilStateCreateInfo depthStencilState = vks::initializers::PipelineDepthStencilStateCreateInfo(VK_TRUE, VK_TRUE, VK_COMPARE_OP_LESS_OR_EQUAL);
		VkPipelineViewportStateCreateInfo viewportState = vks::initializers::PipelineViewportStateCreateInfo(1, 1, 0);
		VkPipelineMultisampleStateCreateInfo multisampleState = vks::initializers::PipelineMultisampleStateCreateInfo(VK_SAMPLE_COUNT_1_BIT);

		std::vector<VkDynamicState> dynamicStateEnables = 
		{
			VK_DYNAMIC_STATE_VIEWPORT,
			VK_DYNAMIC_STATE_SCISSOR
		};
		VkPipelineDynamicStateCreateInfo dynamicState = vks::initializers::PipelineDynamicStateCreateInfo(dynamicStateEnables);

		VkGraphicsPipelineCreateInfo pipelineCreateInfo = vks::initializers::PipelineCreateInfo(pipelineLayout, renderPass);

		std::array<VkPipelineShaderStageCreateInfo, 2> shaderStages;
		shaderStages[0] = LoadShader(GetAssetPath() + "shaders/dynamicuniformbuffer/base.vert.spv", VK_SHADER_STAGE_VERTEX_BIT);
		shaderStages[1] = LoadShader(GetAssetPath() + "shaders/dynamicuniformbuffer/base.frag.spv", VK_SHADER_STAGE_FRAGMENT_BIT);

		pipelineCreateInfo.pInputAssemblyState = &inputAssemblyState;
		pipelineCreateInfo.pRasterizationState = &rasterizationState;
		pipelineCreateInfo.pColorBlendState = &colorBlendState;
		pipelineCreateInfo.pMultisampleState = &multisampleState;
		pipelineCreateInfo.pViewportState = &viewportState;
		pipelineCreateInfo.pDepthStencilState = &depthStencilState;
		pipelineCreateInfo.pDynamicState = &dynamicState;
		pipelineCreateInfo.stageCount = shaderStages.size();
		pipelineCreateInfo.pStages = shaderStages.data();

		VK_CHECK_RESULT(vkCreateGraphicsPipelines(device, pipelineCache, 1, &pipelineCreateInfo, nullptr, &pipeline));
	}

	// Prepare and initialize uniform buffer containing shader uniforms
	void PrepareUniformBuffers()
	{
		/// Allocate data for the dynamic uniform buffer object
		// We allocate this manually as the alignment of the offset differs between GPUs

		// Calculate required alignment based on minimum device offset alignment
		size_t minUboAlignment = vulkanDevice->properties.limits.minUniformBufferOffsetAlignment;
		dynamicAlignment = sizeof(glm::mat4);
		if (minUboAlignment > 0)
			dynamicAlignment = (dynamicAlignment + minUboAlignment - 1) & ~(minUboAlignment - 1);

		size_t bufferSize = OBJECT_INSTANCES * dynamicAlignment;

		uboDataDynamic.model = reinterpret_cast<glm::mat4*>(AlignedAlloc(bufferSize, dynamicAlignment));
		assert(uboDataDynamic.model);

		std::cout << "minUniformBufferOffsetAlignment = " << minUboAlignment << std::endl;
		std::cout << "dynamicAlignment = " << dynamicAlignment << std::endl;

		// Vertex shader uniform buffer block

		// Static shared uniform buffer object with projection and view matrix
		VK_CHECK_RESULT(vulkanDevice->CreateBuffer(VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, &uniformBuffers.view, sizeof(uboVS)));

		// Uniform buffer object with per-object matrices
		VK_CHECK_RESULT(vulkanDevice->CreateBuffer(VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT, &uniformBuffers.dynamic, bufferSize));

		// Map persistent
		VK_CHECK_RESULT(uniformBuffers.view.Map());
		VK_CHECK_RESULT(uniformBuffers.dynamic.Map());

		// Prepare per-object matrices with offsets and random rotations
		std::default_random_engine randEngine(benchmark.active ? 0 : static_cast<unsigned>(time(nullptr)));
		std::normal_distribution<float> randDist(-1.0f, 1.0f);

		for (auto i = 0; i < OBJECT_INSTANCES; ++i)
		{
			rotations[i] = glm::vec3(randDist(randEngine), randDist(randEngine), randDist(randEngine)) * 2.0f * (float)M_PI;
			rotations[i] = glm::vec3(randDist(randEngine), randDist(randEngine), randDist(randEngine));
		}

		UpdateUniformBuffers();
		UpdateDynamicUniformBuffers();
	}

	void UpdateUniformBuffers()
	{
		uboVS.projection = camera.matrices.perspective;
		uboVS.view = camera.matrices.view;

		memcpy(uniformBuffers.view.mapped, &uboVS, sizeof(uboVS));
	}

	void UpdateDynamicUniformBuffers(bool force = false)
	{
		// Update at max. 60 fps
		animationTimer += frameTimer;
		if ((animationTimer < 1.0f / 60) && !force)
			return;

		// Dynamic ubo with per-object model matrices indexed by offsets in the command buffer
		auto dim = static_cast<uint32_t>(pow(OBJECT_INSTANCES, (1.0f / 3)));
		glm::vec3 offset(5.0f);

		for (auto x = 0; x < dim; x++)
		{
			for (auto y = 0; y < dim; y++)
			{
				for (auto z = 0; z < dim; z++)
				{
					uint32_t index = x * dim * dim + y * dim + z;
					// Aligned offset
					auto modelMat = reinterpret_cast<glm::mat4*>(reinterpret_cast<uint64_t>(uboDataDynamic.model) + index * dynamicAlignment);

					// Update rotations
					rotations[index] += animationTimer * rotationSpeeds[index];

					// Update matrices
					glm::vec3 pos = glm::vec3(
						-((dim * x) / 2.0f) + offset.x / 2.0f + x * offset.x, 
						-((dim * y) / 2.0f) + offset.y / 2.0f + y * offset.y,
						-((dim * z) / 2.0f) + offset.z / 2.0f + z * offset.z
					);
					*modelMat = glm::translate(glm::mat4(1.0f), pos);
					*modelMat = glm::rotate(*modelMat, rotations[index].x, glm::vec3(1.0f, 1.0f, 0.0f));
					*modelMat = glm::rotate(*modelMat, rotations[index].y, glm::vec3(0.0f, 1.0f, 0.0f));
					*modelMat = glm::rotate(*modelMat, rotations[index].z, glm::vec3(0.0f, 0.0f, 1.0f));
				}
			}
		}

		animationTimer = 0.0f;

		memcpy(uniformBuffers.dynamic.mapped, &uboDataDynamic.model, uniformBuffers.dynamic.size);
		// Flush to make changes visible to host
		VkMappedMemoryRange memoryRange = vks::initializers::MappedMemoryRange();
		memoryRange.memory = uniformBuffers.dynamic.memory;
		memoryRange.size = uniformBuffers.dynamic.size;
		vkFlushMappedMemoryRanges(device, 1, &memoryRange);
	}

	void Prepare()
	{
		__super::Prepare();
		GenerateCube();
		SetupVertexDescriptions();
		PrepareUniformBuffers();
		SetupDesctiptorSetLayout();
		PreparePipelines();
		SetupDescriptorPool();
		SetupDesctiptorSet();
		BuildCommandBuffers();
		prepared = true;
	}

	virtual void Render()
	{
		if (!prepared)
			return;

		Draw();
		if (!paused)
			UpdateUniformBuffers();
	}

	virtual void ViewChanged()
	{
		UpdateUniformBuffers();
	}
};

VULKAN_EXAMPLE_MAIN(VulkanExampleDynamicUniformBuffer)
