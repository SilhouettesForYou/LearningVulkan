#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <time.h>
#include <vector>
#include <random>

#define GLM_FORCE_RADIANS
#define GLM_FORCE_DEPTH_ZERO_TO_ONE
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

#include <vulkan/vulkan.h>
#include "VulkanBase.h"
#include "VulkanBuffer.hpp"
#include "VulkanTexture.hpp"
#include "VulkanModel.hpp"

#define M_PI       3.14159265358979323846   // pi
#define VERTEX_BUFFER_BIND_ID 0
#define INSTANCE_BUFFER_BIND_ID 1
#define ENABLE_VALIDATION false
#if defined(__ANDROID__)
#define INSTANCE_COUNT 4096
#else
#define INSTANCE_COUNT 8192
#endif

class VulkanExampleInstancing : public VulkanBase
{
public:
	struct {
		vks::Texture2DArray rocks;
		vks::Texture2D planet;
	} textures;

	// Vertex layout for the models
	vks::VertexLayout vertexLayout = vks::VertexLayout({
		vks::VERTEX_COMPONENT_POSITION,
		vks::VERTEX_COMPONENT_NORMAL,
		vks::VERTEX_COMPONENT_UV,
		vks::VERTEX_COMPONENT_COLOR,
		});

	struct {
		vks::Model rock;
		vks::Model planet;
	} models;

	// Per-instance data block
	struct InstanceData {
		glm::vec3 pos;
		glm::vec3 rot;
		float scale;
		uint32_t texIndex;
	};
	// Contains the instanced data
	struct InstanceBuffer {
		VkBuffer buffer = VK_NULL_HANDLE;
		VkDeviceMemory memory = VK_NULL_HANDLE;
		size_t size = 0;
		VkDescriptorBufferInfo descriptor;
	} instanceBuffer;

	struct UBOVS {
		glm::mat4 projection;
		glm::mat4 view;
		glm::vec4 lightPos = glm::vec4(0.0f, -5.0f, 0.0f, 1.0f);
		float locSpeed = 0.0f;
		float globSpeed = 0.0f;
	} uboVS;

	struct {
		vks::Buffer scene;
	} uniformBuffers;

	VkPipelineLayout pipelineLayout;
	struct {
		VkPipeline instancedRocks;
		VkPipeline planet;
		VkPipeline starfield;
	} pipelines;

	VkDescriptorSetLayout descriptorSetLayout;
	struct {
		VkDescriptorSet instancedRocks;
		VkDescriptorSet planet;
	} descriptorSets;

	VulkanExampleInstancing() : VulkanBase(ENABLE_VALIDATION)
	{
		title = "Instanced mesh rendering";
		camera.type = Camera::CameraType::lookat;
		camera.SetPosition(glm::vec3(5.5f, -1.85f, -18.5f));
		camera.SetRotation(glm::vec3(-17.2f, -4.7f, 0.0f));
		camera.SetPerspective(60.0f, (float)width / (float)height, 1.0f, 256.0f);
		settings.overlay = true;
	}

	~VulkanExampleInstancing()
	{
		vkDestroyPipeline(device, pipelines.instancedRocks, nullptr);
		vkDestroyPipeline(device, pipelines.planet, nullptr);
		vkDestroyPipeline(device, pipelines.starfield, nullptr);
		vkDestroyPipelineLayout(device, pipelineLayout, nullptr);
		vkDestroyDescriptorSetLayout(device, descriptorSetLayout, nullptr);
		vkDestroyBuffer(device, instanceBuffer.buffer, nullptr);
		vkFreeMemory(device, instanceBuffer.memory, nullptr);
		models.rock.Destroy();
		models.planet.Destroy();
		textures.rocks.Destroy();
		textures.planet.Destroy();
		uniformBuffers.scene.Destroy();
	}

	// Enable physical device features required for this example
	virtual void GetEnabledFeatures()
	{
		// Enable anisotropic filtering if supported
		if (deviceFeatures.samplerAnisotropy) {
			enabledFeatures.samplerAnisotropy = VK_TRUE;
		}
		// Enable texture compression
		if (deviceFeatures.textureCompressionBC) {
			enabledFeatures.textureCompressionBC = VK_TRUE;
		}
		else if (deviceFeatures.textureCompressionASTC_LDR) {
			enabledFeatures.textureCompressionASTC_LDR = VK_TRUE;
		}
		else if (deviceFeatures.textureCompressionETC2) {
			enabledFeatures.textureCompressionETC2 = VK_TRUE;
		}
	};

	void BuildCommandBuffers()
	{
		VkCommandBufferBeginInfo cmdBufInfo = vks::initializers::CommandBufferBeginInfo();

		VkClearValue clearValues[2];
		clearValues[0].color = { { 0.0f, 0.0f, 0.2f, 0.0f } };
		clearValues[1].depthStencil = { 1.0f, 0 };

		VkRenderPassBeginInfo renderPassBeginInfo = vks::initializers::RenderPassBeginInfo();
		renderPassBeginInfo.renderPass = renderPass;
		renderPassBeginInfo.renderArea.extent.width = width;
		renderPassBeginInfo.renderArea.extent.height = height;
		renderPassBeginInfo.clearValueCount = 2;
		renderPassBeginInfo.pClearValues = clearValues;

		for (int32_t i = 0; i < drawCmdBuffers.size(); ++i)
		{
			// Set target frame buffer
			renderPassBeginInfo.framebuffer = frameBuffers[i];

			VK_CHECK_RESULT(vkBeginCommandBuffer(drawCmdBuffers[i], &cmdBufInfo));

			vkCmdBeginRenderPass(drawCmdBuffers[i], &renderPassBeginInfo, VK_SUBPASS_CONTENTS_INLINE);

			VkViewport viewport = vks::initializers::Viewport((float)width, (float)height, 0.0f, 1.0f);
			vkCmdSetViewport(drawCmdBuffers[i], 0, 1, &viewport);

			VkRect2D scissor = vks::initializers::Rect2D(width, height, 0, 0);
			vkCmdSetScissor(drawCmdBuffers[i], 0, 1, &scissor);

			VkDeviceSize offsets[1] = { 0 };

			// Star field
			vkCmdBindDescriptorSets(drawCmdBuffers[i], VK_PIPELINE_BIND_POINT_GRAPHICS, pipelineLayout, 0, 1, &descriptorSets.planet, 0, NULL);
			vkCmdBindPipeline(drawCmdBuffers[i], VK_PIPELINE_BIND_POINT_GRAPHICS, pipelines.starfield);
			vkCmdDraw(drawCmdBuffers[i], 4, 1, 0, 0);

			// Planet
			vkCmdBindDescriptorSets(drawCmdBuffers[i], VK_PIPELINE_BIND_POINT_GRAPHICS, pipelineLayout, 0, 1, &descriptorSets.planet, 0, NULL);
			vkCmdBindPipeline(drawCmdBuffers[i], VK_PIPELINE_BIND_POINT_GRAPHICS, pipelines.planet);
			vkCmdBindVertexBuffers(drawCmdBuffers[i], VERTEX_BUFFER_BIND_ID, 1, &models.planet.vertices.buffer, offsets);
			vkCmdBindIndexBuffer(drawCmdBuffers[i], models.planet.indices.buffer, 0, VK_INDEX_TYPE_UINT32);
			vkCmdDrawIndexed(drawCmdBuffers[i], models.planet.indexCount, 1, 0, 0, 0);

			// Instanced rocks
			vkCmdBindDescriptorSets(drawCmdBuffers[i], VK_PIPELINE_BIND_POINT_GRAPHICS, pipelineLayout, 0, 1, &descriptorSets.instancedRocks, 0, NULL);
			vkCmdBindPipeline(drawCmdBuffers[i], VK_PIPELINE_BIND_POINT_GRAPHICS, pipelines.instancedRocks);
			// Binding point 0 : Mesh vertex buffer
			vkCmdBindVertexBuffers(drawCmdBuffers[i], VERTEX_BUFFER_BIND_ID, 1, &models.rock.vertices.buffer, offsets);
			// Binding point 1 : Instance data buffer
			vkCmdBindVertexBuffers(drawCmdBuffers[i], INSTANCE_BUFFER_BIND_ID, 1, &instanceBuffer.buffer, offsets);

			vkCmdBindIndexBuffer(drawCmdBuffers[i], models.rock.indices.buffer, 0, VK_INDEX_TYPE_UINT32);

			// Render instances
			vkCmdDrawIndexed(drawCmdBuffers[i], models.rock.indexCount, INSTANCE_COUNT, 0, 0, 0);

			DrawUI(drawCmdBuffers[i]);

			vkCmdEndRenderPass(drawCmdBuffers[i]);

			VK_CHECK_RESULT(vkEndCommandBuffer(drawCmdBuffers[i]));
		}
	}

	void LoadAssets()
	{
		models.rock.LoadFromFile(GetAssetPath() + "models/rock01.dae", vertexLayout, 0.1f, vulkanDevice, queue);
		models.planet.LoadFromFile(GetAssetPath() + "models/sphere.obj", vertexLayout, 0.2f, vulkanDevice, queue);

		// Textures
		std::string texFormatSuffix;
		VkFormat texFormat;
		// Get supported compressed texture format
		if (vulkanDevice->features.textureCompressionBC) {
			texFormatSuffix = "_bc3_unorm";
			texFormat = VK_FORMAT_BC3_UNORM_BLOCK;
		}
		else if (vulkanDevice->features.textureCompressionASTC_LDR) {
			texFormatSuffix = "_astc_8x8_unorm";
			texFormat = VK_FORMAT_ASTC_8x8_UNORM_BLOCK;
		}
		else if (vulkanDevice->features.textureCompressionETC2) {
			texFormatSuffix = "_etc2_unorm";
			texFormat = VK_FORMAT_ETC2_R8G8B8_UNORM_BLOCK;
		}
		else {
			vks::tools::ExitFatal("Device does not support any compressed texture format!", VK_ERROR_FEATURE_NOT_PRESENT);
		}

		textures.rocks.LoadFromFile(GetAssetPath() + "textures/texturearray_rocks" + texFormatSuffix + ".ktx", texFormat, vulkanDevice, queue);
		textures.planet.LoadFromFile(GetAssetPath() + "textures/lavaplanet" + texFormatSuffix + ".ktx", texFormat, vulkanDevice, queue);
	}

	void SetupDescriptorPool()
	{
		// Example uses one ubo
		std::vector<VkDescriptorPoolSize> poolSizes =
		{
			vks::initializers::DescriptorPoolSize(VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 2),
			vks::initializers::DescriptorPoolSize(VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 2),
		};

		VkDescriptorPoolCreateInfo descriptorPoolInfo =
			vks::initializers::DescriptorPoolCreateInfo(
				poolSizes.size(),
				poolSizes.data(),
				2);

		VK_CHECK_RESULT(vkCreateDescriptorPool(device, &descriptorPoolInfo, nullptr, &descriptorPool));
	}

	void SetupDescriptorSetLayout()
	{
		std::vector<VkDescriptorSetLayoutBinding> setLayoutBindings =
		{
			// Binding 0 : Vertex shader uniform buffer
			vks::initializers::DescriptorSetLayoutBinding(
				VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
				VK_SHADER_STAGE_VERTEX_BIT,
				0),
			// Binding 1 : Fragment shader combined sampler
			vks::initializers::DescriptorSetLayoutBinding(
				VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
				VK_SHADER_STAGE_FRAGMENT_BIT,
				1),
		};

		VkDescriptorSetLayoutCreateInfo descriptorLayout =
			vks::initializers::DescriptorSetLayoutCreateInfo(
				setLayoutBindings.data(),
				setLayoutBindings.size());

		VK_CHECK_RESULT(vkCreateDescriptorSetLayout(device, &descriptorLayout, nullptr, &descriptorSetLayout));

		VkPipelineLayoutCreateInfo pPipelineLayoutCreateInfo =
			vks::initializers::PipelineLayoutCreateInfo(
				&descriptorSetLayout,
				1);

		VK_CHECK_RESULT(vkCreatePipelineLayout(device, &pPipelineLayoutCreateInfo, nullptr, &pipelineLayout));
	}

	void SetupDescriptorSet()
	{
		VkDescriptorSetAllocateInfo descripotrSetAllocInfo;
		std::vector<VkWriteDescriptorSet> writeDescriptorSets;

		descripotrSetAllocInfo = vks::initializers::DescriptorSetAllocateInfo(descriptorPool, &descriptorSetLayout, 1);;

		// Instanced rocks
		VK_CHECK_RESULT(vkAllocateDescriptorSets(device, &descripotrSetAllocInfo, &descriptorSets.instancedRocks));
		writeDescriptorSets = {
			vks::initializers::WriteDescriptorSet(descriptorSets.instancedRocks, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,	0, &uniformBuffers.scene.descriptor),	// Binding 0 : Vertex shader uniform buffer
			vks::initializers::WriteDescriptorSet(descriptorSets.instancedRocks, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, &textures.rocks.descriptor)	// Binding 1 : Color map
		};
		vkUpdateDescriptorSets(device, writeDescriptorSets.size(), writeDescriptorSets.data(), 0, NULL);

		// Planet
		VK_CHECK_RESULT(vkAllocateDescriptorSets(device, &descripotrSetAllocInfo, &descriptorSets.planet));
		writeDescriptorSets = {
			vks::initializers::WriteDescriptorSet(descriptorSets.planet, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,	0, &uniformBuffers.scene.descriptor),			// Binding 0 : Vertex shader uniform buffer
			vks::initializers::WriteDescriptorSet(descriptorSets.planet, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, &textures.planet.descriptor)			// Binding 1 : Color map
		};
		vkUpdateDescriptorSets(device, writeDescriptorSets.size(), writeDescriptorSets.data(), 0, NULL);

	}

	void PreparePipelines()
	{
		VkPipelineInputAssemblyStateCreateInfo inputAssemblyState =
			vks::initializers::PipelineInputAssemblyStateCreateInfo(
				VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST,
				0,
				VK_FALSE);

		VkPipelineRasterizationStateCreateInfo rasterizationState =
			vks::initializers::PipelineRasterizationStateCreateInfo(
				VK_POLYGON_MODE_FILL,
				VK_CULL_MODE_BACK_BIT,
				VK_FRONT_FACE_CLOCKWISE,
				0);

		VkPipelineColorBlendAttachmentState blendAttachmentState =
			vks::initializers::PipelineColorBlendAttachmentState(
				0xf,
				VK_FALSE);

		VkPipelineColorBlendStateCreateInfo colorBlendState =
			vks::initializers::PipelineColorBlendStateCreateInfo(
				1,
				&blendAttachmentState);

		VkPipelineDepthStencilStateCreateInfo depthStencilState =
			vks::initializers::PipelineDepthStencilStateCreateInfo(
				VK_TRUE,
				VK_TRUE,
				VK_COMPARE_OP_LESS_OR_EQUAL);

		VkPipelineViewportStateCreateInfo viewportState =
			vks::initializers::PipelineViewportStateCreateInfo(1, 1, 0);

		VkPipelineMultisampleStateCreateInfo multisampleState =
			vks::initializers::PipelineMultisampleStateCreateInfo(
				VK_SAMPLE_COUNT_1_BIT,
				0);

		std::vector<VkDynamicState> dynamicStateEnables = {
			VK_DYNAMIC_STATE_VIEWPORT,
			VK_DYNAMIC_STATE_SCISSOR
		};
		VkPipelineDynamicStateCreateInfo dynamicState =
			vks::initializers::PipelineDynamicStateCreateInfo(
				dynamicStateEnables.data(),
				dynamicStateEnables.size(),
				0);

		// Load shaders
		std::array<VkPipelineShaderStageCreateInfo, 2> shaderStages;

		VkGraphicsPipelineCreateInfo pipelineCreateInfo =
			vks::initializers::PipelineCreateInfo(
				pipelineLayout,
				renderPass,
				0);

		pipelineCreateInfo.pInputAssemblyState = &inputAssemblyState;
		pipelineCreateInfo.pRasterizationState = &rasterizationState;
		pipelineCreateInfo.pColorBlendState = &colorBlendState;
		pipelineCreateInfo.pMultisampleState = &multisampleState;
		pipelineCreateInfo.pViewportState = &viewportState;
		pipelineCreateInfo.pDepthStencilState = &depthStencilState;
		pipelineCreateInfo.pDynamicState = &dynamicState;
		pipelineCreateInfo.stageCount = shaderStages.size();
		pipelineCreateInfo.pStages = shaderStages.data();

		// This example uses two different input states, one for the instanced part and one for non-instanced rendering
		VkPipelineVertexInputStateCreateInfo inputState = vks::initializers::PipelineVertexInputStateCreateInfo();
		std::vector<VkVertexInputBindingDescription> bindingDescriptions;
		std::vector<VkVertexInputAttributeDescription> attributeDescriptions;

		// Vertex input bindings
		// The instancing pipeline uses a vertex input state with two bindings
		bindingDescriptions = {
			// Binding point 0: Mesh vertex layout description at per-vertex rate
			vks::initializers::VertexInputBindingDescription(VERTEX_BUFFER_BIND_ID, vertexLayout.Stride(), VK_VERTEX_INPUT_RATE_VERTEX),
			// Binding point 1: Instanced data at per-instance rate
			vks::initializers::VertexInputBindingDescription(INSTANCE_BUFFER_BIND_ID, sizeof(InstanceData), VK_VERTEX_INPUT_RATE_INSTANCE)
		};

		// Vertex attribute bindings
		// Note that the shader declaration for per-vertex and per-instance attributes is the same, the different input rates are only stored in the bindings:
		// instanced.vert:
		//	layout (location = 0) in vec3 inPos;		Per-Vertex
		//	...
		//	layout (location = 4) in vec3 instancePos;	Per-Instance
		attributeDescriptions = {
			// Per-vertex attributees
			// These are advanced for each vertex fetched by the vertex shader
			vks::initializers::VertexInputAttributeDescription(VERTEX_BUFFER_BIND_ID, 0, VK_FORMAT_R32G32B32_SFLOAT, 0),					// Location 0: Position
			vks::initializers::VertexInputAttributeDescription(VERTEX_BUFFER_BIND_ID, 1, VK_FORMAT_R32G32B32_SFLOAT, sizeof(float) * 3),	// Location 1: Normal
			vks::initializers::VertexInputAttributeDescription(VERTEX_BUFFER_BIND_ID, 2, VK_FORMAT_R32G32_SFLOAT, sizeof(float) * 6),		// Location 2: Texture coordinates
			vks::initializers::VertexInputAttributeDescription(VERTEX_BUFFER_BIND_ID, 3, VK_FORMAT_R32G32B32_SFLOAT, sizeof(float) * 8),	// Location 3: Color
			// Per-Instance attributes
			// These are fetched for each instance rendered
			vks::initializers::VertexInputAttributeDescription(INSTANCE_BUFFER_BIND_ID, 4, VK_FORMAT_R32G32B32_SFLOAT, 0),					// Location 4: Position
			vks::initializers::VertexInputAttributeDescription(INSTANCE_BUFFER_BIND_ID, 5, VK_FORMAT_R32G32B32_SFLOAT, sizeof(float) * 3),	// Location 5: Rotation
			vks::initializers::VertexInputAttributeDescription(INSTANCE_BUFFER_BIND_ID, 6, VK_FORMAT_R32_SFLOAT,sizeof(float) * 6),			// Location 6: Scale
			vks::initializers::VertexInputAttributeDescription(INSTANCE_BUFFER_BIND_ID, 7, VK_FORMAT_R32_SINT, sizeof(float) * 7),			// Location 7: Texture array layer index
		};
		inputState.pVertexBindingDescriptions = bindingDescriptions.data();
		inputState.pVertexAttributeDescriptions = attributeDescriptions.data();

		pipelineCreateInfo.pVertexInputState = &inputState;

		// Instancing pipeline
		shaderStages[0] = LoadShader(GetShadersPath() + "instancing/instancing.vert.spv", VK_SHADER_STAGE_VERTEX_BIT);
		shaderStages[1] = LoadShader(GetShadersPath() + "instancing/instancing.frag.spv", VK_SHADER_STAGE_FRAGMENT_BIT);
		// Use all input bindings and attribute descriptions
		inputState.vertexBindingDescriptionCount = static_cast<uint32_t>(bindingDescriptions.size());
		inputState.vertexAttributeDescriptionCount = static_cast<uint32_t>(attributeDescriptions.size());
		VK_CHECK_RESULT(vkCreateGraphicsPipelines(device, pipelineCache, 1, &pipelineCreateInfo, nullptr, &pipelines.instancedRocks));

		// Planet rendering pipeline
		shaderStages[0] = LoadShader(GetShadersPath() + "instancing/planet.vert.spv", VK_SHADER_STAGE_VERTEX_BIT);
		shaderStages[1] = LoadShader(GetShadersPath() + "instancing/planet.frag.spv", VK_SHADER_STAGE_FRAGMENT_BIT);
		// Only use the non-instanced input bindings and attribute descriptions
		inputState.vertexBindingDescriptionCount = 1;
		inputState.vertexAttributeDescriptionCount = 4;
		VK_CHECK_RESULT(vkCreateGraphicsPipelines(device, pipelineCache, 1, &pipelineCreateInfo, nullptr, &pipelines.planet));

		// Star field pipeline
		rasterizationState.cullMode = VK_CULL_MODE_NONE;
		depthStencilState.depthWriteEnable = VK_FALSE;
		shaderStages[0] = LoadShader(GetShadersPath() + "instancing/starfield.vert.spv", VK_SHADER_STAGE_VERTEX_BIT);
		shaderStages[1] = LoadShader(GetShadersPath() + "instancing/starfield.frag.spv", VK_SHADER_STAGE_FRAGMENT_BIT);
		// Vertices are generated in the vertex shader
		inputState.vertexBindingDescriptionCount = 0;
		inputState.vertexAttributeDescriptionCount = 0;
		VK_CHECK_RESULT(vkCreateGraphicsPipelines(device, pipelineCache, 1, &pipelineCreateInfo, nullptr, &pipelines.starfield));
	}

	void PrepareInstanceData()
	{
		std::vector<InstanceData> instanceData;
		instanceData.resize(INSTANCE_COUNT);

		std::default_random_engine rndGenerator(benchmark.active ? 0 : (unsigned)time(nullptr));
		std::uniform_real_distribution<float> uniformDist(0.0, 1.0);
		std::uniform_int_distribution<uint32_t> rndTextureIndex(0, textures.rocks.layerCount);

		// Distribute rocks randomly on two different rings
		for (auto i = 0; i < INSTANCE_COUNT / 2; i++) {
			glm::vec2 ring0{ 7.0f, 11.0f };
			glm::vec2 ring1{ 14.0f, 18.0f };

			float rho, theta;

			// Inner ring
			rho = sqrt((pow(ring0[1], 2.0f) - pow(ring0[0], 2.0f)) * uniformDist(rndGenerator) + pow(ring0[0], 2.0f));
			theta = 2.0 * M_PI * uniformDist(rndGenerator);
			instanceData[i].pos = glm::vec3(rho * cos(theta), uniformDist(rndGenerator) * 0.5f - 0.25f, rho * sin(theta));
			instanceData[i].rot = glm::vec3(M_PI * uniformDist(rndGenerator), M_PI * uniformDist(rndGenerator), M_PI * uniformDist(rndGenerator));
			instanceData[i].scale = 1.5f + uniformDist(rndGenerator) - uniformDist(rndGenerator);
			instanceData[i].texIndex = rndTextureIndex(rndGenerator);
			instanceData[i].scale *= 0.75f;

			// Outer ring
			rho = sqrt((pow(ring1[1], 2.0f) - pow(ring1[0], 2.0f)) * uniformDist(rndGenerator) + pow(ring1[0], 2.0f));
			theta = 2.0 * M_PI * uniformDist(rndGenerator);
			instanceData[i + INSTANCE_COUNT / 2].pos = glm::vec3(rho * cos(theta), uniformDist(rndGenerator) * 0.5f - 0.25f, rho * sin(theta));
			instanceData[i + INSTANCE_COUNT / 2].rot = glm::vec3(M_PI * uniformDist(rndGenerator), M_PI * uniformDist(rndGenerator), M_PI * uniformDist(rndGenerator));
			instanceData[i + INSTANCE_COUNT / 2].scale = 1.5f + uniformDist(rndGenerator) - uniformDist(rndGenerator);
			instanceData[i + INSTANCE_COUNT / 2].texIndex = rndTextureIndex(rndGenerator);
			instanceData[i + INSTANCE_COUNT / 2].scale *= 0.75f;
		}

		instanceBuffer.size = instanceData.size() * sizeof(InstanceData);

		// Staging
		// Instanced data is static, copy to device local memory
		// This results in better performance

		struct {
			VkDeviceMemory memory;
			VkBuffer buffer;
		} stagingBuffer;

		VK_CHECK_RESULT(vulkanDevice->CreateBuffer(
			VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
			VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
			instanceBuffer.size,
			&stagingBuffer.buffer,
			&stagingBuffer.memory,
			instanceData.data()));

		VK_CHECK_RESULT(vulkanDevice->CreateBuffer(
			VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
			VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
			instanceBuffer.size,
			&instanceBuffer.buffer,
			&instanceBuffer.memory));

		// Copy to staging buffer
		VkCommandBuffer copyCmd = vulkanDevice->CreateCommandBuffer(VK_COMMAND_BUFFER_LEVEL_PRIMARY, true);

		VkBufferCopy copyRegion = { };
		copyRegion.size = instanceBuffer.size;
		vkCmdCopyBuffer(
			copyCmd,
			stagingBuffer.buffer,
			instanceBuffer.buffer,
			1,
			&copyRegion);

		vulkanDevice->FlushCommandBuffer(copyCmd, queue, true);

		instanceBuffer.descriptor.range = instanceBuffer.size;
		instanceBuffer.descriptor.buffer = instanceBuffer.buffer;
		instanceBuffer.descriptor.offset = 0;

		// Destroy staging resources
		vkDestroyBuffer(device, stagingBuffer.buffer, nullptr);
		vkFreeMemory(device, stagingBuffer.memory, nullptr);
	}

	void PrepareUniformBuffers()
	{
		VK_CHECK_RESULT(vulkanDevice->CreateBuffer(
			VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
			VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
			&uniformBuffers.scene,
			sizeof(uboVS)));

		// Map persistent
		VK_CHECK_RESULT(uniformBuffers.scene.Map());

		UpdateUniformBuffer(true);
	}

	void UpdateUniformBuffer(bool viewChanged)
	{
		if (viewChanged)
		{
			uboVS.projection = camera.matrices.perspective;
			uboVS.view = camera.matrices.view;
		}

		if (!paused)
		{
			uboVS.locSpeed += frameTimer * 0.35f;
			uboVS.globSpeed += frameTimer * 0.01f;
		}

		memcpy(uniformBuffers.scene.mapped, &uboVS, sizeof(uboVS));
	}

	void Draw()
	{
		__super::PrepareFrame();

		// Command buffer to be sumitted to the queue
		submitInfo.commandBufferCount = 1;
		submitInfo.pCommandBuffers = &drawCmdBuffers[currentBuffer];

		// Submit to queue
		VK_CHECK_RESULT(vkQueueSubmit(queue, 1, &submitInfo, VK_NULL_HANDLE));

		__super::SubmitFrame();
	}

	void Prepare()
	{
		__super::Prepare();
		LoadAssets();
		PrepareInstanceData();
		PrepareUniformBuffers();
		SetupDescriptorSetLayout();
		PreparePipelines();
		SetupDescriptorPool();
		SetupDescriptorSet();
		BuildCommandBuffers();
		prepared = true;
	}

	virtual void Render()
	{
		if (!prepared)
		{
			return;
		}
		Draw();
		if (!paused)
		{
			UpdateUniformBuffer(false);
		}
	}

	virtual void ViewChanged()
	{
		UpdateUniformBuffer(true);
	}

	virtual void OnUpdateUIOverlay(vks::UIOverlay* overlay)
	{
		if (overlay->Header("Statistics")) {
			overlay->Text("Instances: %d", INSTANCE_COUNT);
		}
	}
};

VULKAN_EXAMPLE_MAIN(VulkanExampleInstancing)