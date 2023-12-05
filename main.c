#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>

#define SDL_MAIN_HANDLED
#include <SDL.h>

#include <Refresh.h>
#include <Refresh_Image.h>

typedef struct Vertex
{
	float x, y, z;
	float u, v;
} Vertex;

typedef struct RaymarchUniforms
{
	float time;
	float padding;
	float resolutionX;
	float resolutionY;
} RaymarchUniforms;

int main(int argc, char *argv[])
{
	if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_TIMER | SDL_INIT_GAMECONTROLLER) < 0)
	{
		fprintf(stderr, "Failed to initialize SDL\n\t%s\n", SDL_GetError());
		return -1;
	}

	const int windowWidth = 1280;
	const int windowHeight = 720;

	uint32_t windowFlags;

	Refresh_Backend backend = Refresh_SelectBackend(REFRESH_BACKEND_VULKAN, &windowFlags);

	SDL_Window *window = SDL_CreateWindow(
		"Refresh Test",
		SDL_WINDOWPOS_UNDEFINED,
		SDL_WINDOWPOS_UNDEFINED,
		windowWidth,
		windowHeight,
		windowFlags | SDL_WINDOW_RESIZABLE
	);

	Refresh_Device *device = Refresh_CreateDevice(1);

	Refresh_ClaimWindow(device, window, REFRESH_PRESENTMODE_MAILBOX);

	bool quit = false;

	double t = 0.0;
	double dt = 0.01;

	uint64_t currentTime = SDL_GetPerformanceCounter();
	double accumulator = 0.0;

	Refresh_Rect renderArea;
	renderArea.x = 0;
	renderArea.y = 0;
	renderArea.w = windowWidth;
	renderArea.h = windowHeight;

	/* Compile shaders */

	SDL_RWops* file = SDL_RWFromFile("passthrough.vert.refresh", "rb");
	Sint64 shaderCodeSize = SDL_RWsize(file);
	uint32_t* byteCode = SDL_malloc(sizeof(uint32_t) * shaderCodeSize);
	SDL_RWread(file, byteCode, sizeof(uint32_t), shaderCodeSize);
	SDL_RWclose(file);

	Refresh_ShaderModuleCreateInfo passthroughVertexShaderModuleCreateInfo;
	passthroughVertexShaderModuleCreateInfo.byteCode = byteCode;
	passthroughVertexShaderModuleCreateInfo.codeSize = shaderCodeSize;

	Refresh_ShaderModule* passthroughVertexShaderModule = Refresh_CreateShaderModule(device, &passthroughVertexShaderModuleCreateInfo);

	file = SDL_RWFromFile("hexagon_grid.frag.refresh", "rb");
	shaderCodeSize = SDL_RWsize(file);
	byteCode = SDL_realloc(byteCode, sizeof(uint32_t) * shaderCodeSize);
	SDL_RWread(file, byteCode, sizeof(uint32_t), shaderCodeSize);
	SDL_RWclose(file);

	Refresh_ShaderModuleCreateInfo raymarchFragmentShaderModuleCreateInfo;
	raymarchFragmentShaderModuleCreateInfo.byteCode = byteCode;
	raymarchFragmentShaderModuleCreateInfo.codeSize = shaderCodeSize;

	Refresh_ShaderModule* raymarchFragmentShaderModule = Refresh_CreateShaderModule(device, &raymarchFragmentShaderModuleCreateInfo);

	SDL_free(byteCode);

	/* Load textures */

	Refresh_CommandBuffer* loadCommandBuffer = Refresh_AcquireCommandBuffer(device);

	size_t textureDataLength;

	void* woodTextureData = SDL_LoadFile("woodgrain.png", &textureDataLength);

	int32_t textureWidth, textureHeight, numChannels;
	uint8_t *woodTexturePixels = Refresh_Image_Load(
		woodTextureData,
		textureDataLength,
		&textureWidth,
		&textureHeight,
		&numChannels
	);

	SDL_free(woodTextureData);

	Refresh_TextureCreateInfo textureCreateInfo;
	textureCreateInfo.width = textureWidth;
	textureCreateInfo.height = textureHeight;
	textureCreateInfo.depth = 1;
	textureCreateInfo.format = REFRESH_TEXTUREFORMAT_R8G8B8A8;
	textureCreateInfo.isCube = 0;
	textureCreateInfo.levelCount = 1;
	textureCreateInfo.sampleCount = REFRESH_SAMPLECOUNT_1;
	textureCreateInfo.usageFlags = REFRESH_TEXTUREUSAGE_SAMPLER_BIT;

	Refresh_Texture *woodTexture = Refresh_CreateTexture(
		device,
		&textureCreateInfo
	);

	Refresh_TextureSlice setTextureDataSlice;
	setTextureDataSlice.texture = woodTexture;
	setTextureDataSlice.rectangle.x = 0;
	setTextureDataSlice.rectangle.y = 0;
	setTextureDataSlice.rectangle.w = textureWidth;
	setTextureDataSlice.rectangle.h = textureHeight;
	setTextureDataSlice.depth = 0;
	setTextureDataSlice.layer = 0;
	setTextureDataSlice.level = 0;

	Refresh_SetTextureData(
		device,
		loadCommandBuffer,
		&setTextureDataSlice,
		woodTexturePixels,
		textureWidth * textureHeight * 4
	);

	Refresh_Image_Free(woodTexturePixels);

	void* noiseTextureData = SDL_LoadFile("noise.png", &textureDataLength);

	uint8_t *noiseTexturePixels = Refresh_Image_Load(
		noiseTextureData,
		textureDataLength,
		&textureWidth,
		&textureHeight,
		&numChannels
	);

	SDL_free(noiseTextureData);

	textureCreateInfo.width = textureWidth;
	textureCreateInfo.height = textureHeight;

	Refresh_Texture *noiseTexture = Refresh_CreateTexture(
		device,
		&textureCreateInfo
	);

	setTextureDataSlice.texture = noiseTexture;
	setTextureDataSlice.rectangle.w = textureWidth;
	setTextureDataSlice.rectangle.h = textureHeight;

	Refresh_SetTextureData(
		device,
		loadCommandBuffer,
		&setTextureDataSlice,
		noiseTexturePixels,
		textureWidth * textureHeight * 4
	);

	Refresh_Image_Free(noiseTexturePixels);

	/* Define vertex buffer */

	Vertex* vertices = SDL_malloc(sizeof(Vertex) * 3);
	vertices[0].x = -1;
	vertices[0].y = -1;
	vertices[0].z = 0;
	vertices[0].u = 0;
	vertices[0].v = 1;

	vertices[1].x = 3;
	vertices[1].y = -1;
	vertices[1].z = 0;
	vertices[1].u = 1;
	vertices[1].v = 1;

	vertices[2].x = -1;
	vertices[2].y = 3;
	vertices[2].z = 0;
	vertices[2].u = 0;
	vertices[2].v = 0;

	Refresh_Buffer* vertexBuffer = Refresh_CreateBuffer(device, REFRESH_BUFFERUSAGE_VERTEX_BIT, sizeof(Vertex) * 3);
	Refresh_SetBufferData(device, loadCommandBuffer, vertexBuffer, 0, vertices, sizeof(Vertex) * 3);

	Refresh_Submit(device, loadCommandBuffer);

	SDL_free(vertices);

	uint64_t* offsets = SDL_malloc(sizeof(uint64_t));
	offsets[0] = 0;

	/* Uniforms struct */

	RaymarchUniforms raymarchUniforms;
	raymarchUniforms.time = 0;
	raymarchUniforms.padding = 0;
	raymarchUniforms.resolutionX = (float)windowWidth;
	raymarchUniforms.resolutionY = (float)windowHeight;

	/* Define pipeline */

	Refresh_DepthStencilState depthStencilState;
	depthStencilState.depthTestEnable = 0;
	depthStencilState.backStencilState.compareMask = 0;
	depthStencilState.backStencilState.compareOp = REFRESH_COMPAREOP_NEVER;
	depthStencilState.backStencilState.depthFailOp = REFRESH_STENCILOP_ZERO;
	depthStencilState.backStencilState.failOp = REFRESH_STENCILOP_ZERO;
	depthStencilState.backStencilState.passOp = REFRESH_STENCILOP_ZERO;
	depthStencilState.backStencilState.reference = 0;
	depthStencilState.backStencilState.writeMask = 0;
	depthStencilState.compareOp = REFRESH_COMPAREOP_NEVER;
	depthStencilState.depthBoundsTestEnable = 0;
	depthStencilState.depthWriteEnable = 0;
	depthStencilState.frontStencilState.compareMask = 0;
	depthStencilState.frontStencilState.compareOp = REFRESH_COMPAREOP_NEVER;
	depthStencilState.frontStencilState.depthFailOp = REFRESH_STENCILOP_ZERO;
	depthStencilState.frontStencilState.failOp = REFRESH_STENCILOP_ZERO;
	depthStencilState.frontStencilState.passOp = REFRESH_STENCILOP_ZERO;
	depthStencilState.frontStencilState.reference = 0;
	depthStencilState.frontStencilState.writeMask = 0;
	depthStencilState.maxDepthBounds = 1.0f;
	depthStencilState.minDepthBounds = 0.0f;
	depthStencilState.stencilTestEnable = 0;

	Refresh_GraphicsShaderInfo vertexShaderInfo;
	vertexShaderInfo.shaderModule = passthroughVertexShaderModule;
	vertexShaderInfo.entryPointName = "main";
	vertexShaderInfo.uniformBufferSize = 0;
	vertexShaderInfo.samplerBindingCount = 0;

	Refresh_GraphicsShaderInfo fragmentShaderInfo;
	fragmentShaderInfo.shaderModule = raymarchFragmentShaderModule;
	fragmentShaderInfo.entryPointName = "main";
	fragmentShaderInfo.uniformBufferSize = sizeof(RaymarchUniforms);
	fragmentShaderInfo.samplerBindingCount = 2;

	Refresh_MultisampleState multisampleState;
	multisampleState.multisampleCount = REFRESH_SAMPLECOUNT_1;
	multisampleState.sampleMask = -1;

	Refresh_RasterizerState rasterizerState;
	rasterizerState.cullMode = REFRESH_CULLMODE_NONE;
	rasterizerState.depthBiasClamp = 0;
	rasterizerState.depthBiasConstantFactor = 0;
	rasterizerState.depthBiasEnable = 0;
	rasterizerState.depthBiasSlopeFactor = 0;
	rasterizerState.fillMode = REFRESH_FILLMODE_FILL;
	rasterizerState.frontFace = REFRESH_FRONTFACE_CLOCKWISE;

	Refresh_VertexBinding vertexBinding;
	vertexBinding.binding = 0;
	vertexBinding.inputRate = REFRESH_VERTEXINPUTRATE_VERTEX;
	vertexBinding.stride = sizeof(Vertex);

	Refresh_VertexAttribute *vertexAttributes = SDL_stack_alloc(Refresh_VertexAttribute, 2);
	vertexAttributes[0].binding = 0;
	vertexAttributes[0].location = 0;
	vertexAttributes[0].format = REFRESH_VERTEXELEMENTFORMAT_VECTOR3;
	vertexAttributes[0].offset = 0;

	vertexAttributes[1].binding = 0;
	vertexAttributes[1].location = 1;
	vertexAttributes[1].format = REFRESH_VERTEXELEMENTFORMAT_VECTOR2;
	vertexAttributes[1].offset = sizeof(float) * 3;

	Refresh_VertexInputState vertexInputState;
	vertexInputState.vertexBindings = &vertexBinding;
	vertexInputState.vertexBindingCount = 1;
	vertexInputState.vertexAttributes = vertexAttributes;
	vertexInputState.vertexAttributeCount = 2;

	Refresh_Viewport viewport;
	viewport.x = 0;
	viewport.y = (float)windowHeight;
	viewport.w = (float)windowWidth;
	viewport.h = -(float)windowHeight;
	viewport.minDepth = 0;
	viewport.maxDepth = 1;

	Refresh_ColorAttachmentBlendState renderTargetBlendState;
	renderTargetBlendState.blendEnable = 0;
	renderTargetBlendState.alphaBlendOp = 0;
	renderTargetBlendState.colorBlendOp = 0;
	renderTargetBlendState.colorWriteMask =
		REFRESH_COLORCOMPONENT_R_BIT |
		REFRESH_COLORCOMPONENT_G_BIT |
		REFRESH_COLORCOMPONENT_B_BIT |
		REFRESH_COLORCOMPONENT_A_BIT;
	renderTargetBlendState.dstAlphaBlendFactor = 0;
	renderTargetBlendState.dstColorBlendFactor = 0;
	renderTargetBlendState.srcAlphaBlendFactor = 0;
	renderTargetBlendState.srcColorBlendFactor = 0;

	Refresh_ColorAttachmentDescription colorAttachmentDescription;
	colorAttachmentDescription.format = Refresh_GetSwapchainFormat(device, window);
	colorAttachmentDescription.blendState = renderTargetBlendState;

	Refresh_GraphicsPipelineAttachmentInfo attachmentInfo;
	attachmentInfo.colorAttachmentCount = 1;
	attachmentInfo.colorAttachmentDescriptions = &colorAttachmentDescription;
	attachmentInfo.hasDepthStencilAttachment = 0;
	attachmentInfo.depthStencilFormat = 0;

	Refresh_GraphicsPipelineCreateInfo raymarchPipelineCreateInfo;
	raymarchPipelineCreateInfo.depthStencilState = depthStencilState;
	raymarchPipelineCreateInfo.vertexShaderInfo = vertexShaderInfo;
	raymarchPipelineCreateInfo.fragmentShaderInfo = fragmentShaderInfo;
	raymarchPipelineCreateInfo.multisampleState = multisampleState;
	raymarchPipelineCreateInfo.rasterizerState = rasterizerState;
	raymarchPipelineCreateInfo.primitiveType = REFRESH_PRIMITIVETYPE_TRIANGLELIST;
	raymarchPipelineCreateInfo.vertexInputState = vertexInputState;
	raymarchPipelineCreateInfo.attachmentInfo = attachmentInfo;
	raymarchPipelineCreateInfo.blendConstants[0] = 0.0f;
	raymarchPipelineCreateInfo.blendConstants[1] = 0.0f;
	raymarchPipelineCreateInfo.blendConstants[2] = 0.0f;
	raymarchPipelineCreateInfo.blendConstants[3] = 0.0f;

	Refresh_GraphicsPipeline* raymarchPipeline = Refresh_CreateGraphicsPipeline(device, &raymarchPipelineCreateInfo);

	Refresh_Vec4 clearColor;
	clearColor.x = 100;
	clearColor.y = 149;
	clearColor.z = 237;
	clearColor.w = 255;

	Refresh_DepthStencilValue depthStencilClear;
	depthStencilClear.depth = 1.0f;
	depthStencilClear.stencil = 0;

	/* Sampling */

	Refresh_SamplerStateCreateInfo samplerStateCreateInfo;
	samplerStateCreateInfo.addressModeU = REFRESH_SAMPLERADDRESSMODE_REPEAT;
	samplerStateCreateInfo.addressModeV = REFRESH_SAMPLERADDRESSMODE_REPEAT;
	samplerStateCreateInfo.addressModeW = REFRESH_SAMPLERADDRESSMODE_REPEAT;
	samplerStateCreateInfo.anisotropyEnable = 0;
	samplerStateCreateInfo.borderColor = REFRESH_BORDERCOLOR_FLOAT_OPAQUE_BLACK;
	samplerStateCreateInfo.compareEnable = 0;
	samplerStateCreateInfo.compareOp = REFRESH_COMPAREOP_NEVER;
	samplerStateCreateInfo.magFilter = REFRESH_FILTER_LINEAR;
	samplerStateCreateInfo.maxAnisotropy = 0;
	samplerStateCreateInfo.maxLod = 1;
	samplerStateCreateInfo.minFilter = REFRESH_FILTER_LINEAR;
	samplerStateCreateInfo.minLod = 1;
	samplerStateCreateInfo.mipLodBias = 1;
	samplerStateCreateInfo.mipmapMode = REFRESH_SAMPLERMIPMAPMODE_LINEAR;

	Refresh_Sampler *sampler = Refresh_CreateSampler(
		device,
		&samplerStateCreateInfo
	);

	Refresh_Texture* sampleTextures[2];
	sampleTextures[0] = woodTexture;
	sampleTextures[1] = noiseTexture;

	Refresh_Sampler* sampleSamplers[2];
	sampleSamplers[0] = sampler;
	sampleSamplers[1] = sampler;

	Refresh_Rect flip;
	flip.x = 0;
	flip.y = windowHeight;
	flip.w = windowWidth;
	flip.h = -windowHeight;

	uint8_t screenshotKey = 0;
	uint8_t *screenshotPixels = SDL_malloc(sizeof(uint8_t) * windowWidth * windowHeight * 4);
	Refresh_Buffer *screenshotBuffer = Refresh_CreateBuffer(device, 0, windowWidth * windowHeight * 4);

	double bufferChurnTimer = 0;
	Refresh_Buffer* testBuffer;
	Refresh_Buffer* testBufferTwo;

	while (!quit)
	{
		SDL_Event event;
		while (SDL_PollEvent(&event))
		{
			switch (event.type)
			{
			case SDL_QUIT:
				quit = true;
				break;
			}
		}

		uint64_t newTime = SDL_GetPerformanceCounter();
		double frameTime = (newTime - currentTime) / (double)SDL_GetPerformanceFrequency();

		if (frameTime > 0.25)
			frameTime = 0.25;
		currentTime = newTime;

		accumulator += frameTime;

		bool updateThisLoop = (accumulator >= dt);

		while (accumulator >= dt && !quit)
		{
			// Update here!

			t += dt;
			accumulator -= dt;

			const uint8_t *keyboardState = SDL_GetKeyboardState(NULL);

			if (keyboardState[SDL_SCANCODE_S])
			{
				if (screenshotKey == 1)
				{
					screenshotKey = 2;
				}
				else
				{
					screenshotKey = 1;
				}
			}
			else
			{
				screenshotKey = 0;
			}

			double bufferChurnTimeBefore = bufferChurnTimer;

			bufferChurnTimer += dt;

			if (bufferChurnTimeBefore <= 1 && bufferChurnTimer > 1)
			{
				testBuffer = Refresh_CreateBuffer(device, REFRESH_BUFFERUSAGE_VERTEX_BIT, 128000);
				testBufferTwo = Refresh_CreateBuffer(device, REFRESH_BUFFERUSAGE_VERTEX_BIT, 128000);
			}
			else if (bufferChurnTimeBefore <= 3 && bufferChurnTimer > 3)
			{
				Refresh_QueueDestroyBuffer(device, testBuffer);
				testBuffer = NULL;
			}
			else if (bufferChurnTimeBefore <= 5 && bufferChurnTimer > 5)
			{
				Refresh_QueueDestroyBuffer(device, testBufferTwo);
				testBufferTwo = NULL;
				bufferChurnTimer = 0;
			}
		}

		if (updateThisLoop && !quit)
		{
			// Draw here!

			Refresh_CommandBuffer *commandBuffer = Refresh_AcquireCommandBuffer(device);

			uint32_t swapchainWidth;
			uint32_t swapchainHeight;
			Refresh_Texture *texture = Refresh_AcquireSwapchainTexture(device, commandBuffer, window, &swapchainWidth, &swapchainHeight);

			if (texture != NULL)
			{
				renderArea.w = swapchainWidth;
				renderArea.h = swapchainHeight;

				Refresh_ColorAttachmentInfo colorTargetInfo;
				colorTargetInfo.texture = texture;
				colorTargetInfo.depth = 0;
				colorTargetInfo.layer = 0;
				colorTargetInfo.level = 0;
				colorTargetInfo.loadOp = REFRESH_LOADOP_CLEAR;
				colorTargetInfo.storeOp = REFRESH_STOREOP_STORE;
				colorTargetInfo.clearColor = clearColor;

				Refresh_BeginRenderPass(
					device,
					commandBuffer,
					&colorTargetInfo,
					1,
					NULL
				);

				Refresh_BindGraphicsPipeline(
					device,
					commandBuffer,
					raymarchPipeline
				);

				raymarchUniforms.time = (float)t;
				raymarchUniforms.resolutionX = swapchainWidth;
				raymarchUniforms.resolutionY = swapchainHeight;

				uint32_t fragmentParamOffset = Refresh_PushFragmentShaderUniforms(device, commandBuffer, &raymarchUniforms, sizeof(RaymarchUniforms));
				Refresh_BindVertexBuffers(device, commandBuffer, 0, 1, &vertexBuffer, offsets);
				Refresh_BindFragmentSamplers(device, commandBuffer, sampleTextures, sampleSamplers);
				Refresh_DrawPrimitives(device, commandBuffer, 0, 1, 0, fragmentParamOffset);

				Refresh_EndRenderPass(device, commandBuffer);

				if (screenshotKey == 1)
				{
					SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION, "screenshot!");
					Refresh_TextureSlice screenshotSlice;
					screenshotSlice.depth = 0;
					screenshotSlice.layer = 0;
					screenshotSlice.level = 0;
					screenshotSlice.rectangle.x = 0;
					screenshotSlice.rectangle.y = 0;
					screenshotSlice.rectangle.w = windowWidth;
					screenshotSlice.rectangle.h = windowHeight;
					screenshotSlice.texture = texture;
					Refresh_CopyTextureToBuffer(device, commandBuffer, &screenshotSlice, screenshotBuffer);
				}

				Refresh_Submit(device, commandBuffer);

				if (screenshotKey == 1)
				{
					Refresh_Wait(device);
					Refresh_TextureFormat swapchainFormat = Refresh_GetSwapchainFormat(device, window);
					Refresh_GetBufferData(device, screenshotBuffer, screenshotPixels, windowWidth * windowHeight * 4);

					if (swapchainFormat == REFRESH_TEXTUREFORMAT_B8G8R8A8)
					{
						size_t byteCount = windowWidth * windowHeight * 4;

						uint8_t* rgbaPixels = SDL_malloc(byteCount);

						for (int i = 0; i < byteCount; i += 4)
						{
							rgbaPixels[i]     = screenshotPixels[i + 2];
							rgbaPixels[i + 1] = screenshotPixels[i + 1];
							rgbaPixels[i + 2] = screenshotPixels[i];
							rgbaPixels[i + 3] = screenshotPixels[i + 3];
						}

						Refresh_Image_SavePNG("screenshot.png", rgbaPixels, windowWidth, windowHeight);
						SDL_free(rgbaPixels);
					}
					else
					{
						Refresh_Image_SavePNG("screenshot.png", screenshotPixels, windowWidth, windowHeight);
					}
				}
			}
		}
	}

	SDL_free(offsets);
	SDL_free(screenshotPixels);

	Refresh_QueueDestroyTexture(device, woodTexture);
	Refresh_QueueDestroyTexture(device, noiseTexture);
	Refresh_QueueDestroySampler(device, sampler);

	Refresh_QueueDestroyBuffer(device, vertexBuffer);
	Refresh_QueueDestroyBuffer(device, screenshotBuffer);

	if (testBuffer != NULL)
	{
		Refresh_QueueDestroyBuffer(device, testBuffer);
	}

	if (testBufferTwo != NULL)
	{
		Refresh_QueueDestroyBuffer(device, testBufferTwo);
	}

	Refresh_QueueDestroyGraphicsPipeline(device, raymarchPipeline);

	Refresh_QueueDestroyShaderModule(device, passthroughVertexShaderModule);
	Refresh_QueueDestroyShaderModule(device, raymarchFragmentShaderModule);

	Refresh_DestroyDevice(device);

	SDL_DestroyWindow(window);
	SDL_Quit();

	return 0;
}
