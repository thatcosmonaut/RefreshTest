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
	float time, padding;
	float resolutionX, resolutionY;
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

	SDL_Window *window = SDL_CreateWindow(
		"Refresh Test",
		SDL_WINDOWPOS_UNDEFINED,
		SDL_WINDOWPOS_UNDEFINED,
		windowWidth,
		windowHeight,
		SDL_WINDOW_VULKAN
	);

	REFRESH_PresentationParameters presentationParameters;
	presentationParameters.deviceWindowHandle = window;
	presentationParameters.presentMode = REFRESH_PRESENTMODE_IMMEDIATE;

	REFRESH_Device *device = REFRESH_CreateDevice(&presentationParameters, 1);

	bool quit = false;

	double t = 0.0;
	double dt = 0.01;

	uint64_t currentTime = SDL_GetPerformanceCounter();
	double accumulator = 0.0;

	REFRESH_Rect renderArea;
	renderArea.x = 0;
	renderArea.y = 0;
	renderArea.w = windowWidth;
	renderArea.h = windowHeight;

	/* Compile shaders */

	SDL_RWops* file = SDL_RWFromFile("passthrough_vert.spv", "rb");
	Sint64 shaderCodeSize = SDL_RWsize(file);
	uint32_t* byteCode = SDL_malloc(sizeof(uint32_t) * shaderCodeSize);
	SDL_RWread(file, byteCode, sizeof(uint32_t), shaderCodeSize);
	SDL_RWclose(file);

	REFRESH_ShaderModuleCreateInfo passthroughVertexShaderModuleCreateInfo;
	passthroughVertexShaderModuleCreateInfo.byteCode = byteCode;
	passthroughVertexShaderModuleCreateInfo.codeSize = shaderCodeSize;

	REFRESH_ShaderModule* passthroughVertexShaderModule = REFRESH_CreateShaderModule(device, &passthroughVertexShaderModuleCreateInfo);

	file = SDL_RWFromFile("hexagon_grid.spv", "rb");
	shaderCodeSize = SDL_RWsize(file);
	byteCode = SDL_realloc(byteCode, sizeof(uint32_t) * shaderCodeSize);
	SDL_RWread(file, byteCode, sizeof(uint32_t), shaderCodeSize);
	SDL_RWclose(file);

	REFRESH_ShaderModuleCreateInfo raymarchFragmentShaderModuleCreateInfo;
	raymarchFragmentShaderModuleCreateInfo.byteCode = byteCode;
	raymarchFragmentShaderModuleCreateInfo.codeSize = shaderCodeSize;

	REFRESH_ShaderModule* raymarchFragmentShaderModule = REFRESH_CreateShaderModule(device, &raymarchFragmentShaderModuleCreateInfo);

	SDL_free(byteCode);

	/* Load textures */

	int32_t textureWidth, textureHeight, numChannels;
	uint8_t *woodTexturePixels = REFRESH_Image_Load(
		"woodgrain.png",
		&textureWidth,
		&textureHeight,
		&numChannels
	);

	REFRESH_Texture *woodTexture = REFRESH_CreateTexture2D(
		device,
		REFRESH_SURFACEFORMAT_R8G8B8A8,
		textureWidth,
		textureHeight,
		1,
		REFRESH_TEXTUREUSAGE_SAMPLER_BIT
	);

	REFRESH_TextureSlice setTextureDataSlice;
	setTextureDataSlice.texture = woodTexture;
	setTextureDataSlice.rectangle.x = 0;
	setTextureDataSlice.rectangle.y = 0;
	setTextureDataSlice.rectangle.w = textureWidth;
	setTextureDataSlice.rectangle.h = textureHeight;
	setTextureDataSlice.depth = 0;
	setTextureDataSlice.layer = 0;
	setTextureDataSlice.level = 0;

	REFRESH_SetTextureData(
		device,
		&setTextureDataSlice,
		woodTexturePixels,
		textureWidth * textureHeight * 4
	);

	REFRESH_Image_Free(woodTexturePixels);

	uint8_t *noiseTexturePixels = REFRESH_Image_Load(
		"noise.png",
		&textureWidth,
		&textureHeight,
		&numChannels
	);

	REFRESH_Texture *noiseTexture = REFRESH_CreateTexture2D(
		device,
		REFRESH_SURFACEFORMAT_R8G8B8A8,
		textureWidth,
		textureHeight,
		1,
		REFRESH_TEXTUREUSAGE_SAMPLER_BIT
	);

	setTextureDataSlice.texture = noiseTexture;
	setTextureDataSlice.rectangle.w = textureWidth;
	setTextureDataSlice.rectangle.h = textureHeight;

	REFRESH_SetTextureData(
		device,
		&setTextureDataSlice,
		noiseTexturePixels,
		textureWidth * textureHeight * 4
	);

	REFRESH_Image_Free(noiseTexturePixels);

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

	REFRESH_Buffer* vertexBuffer = REFRESH_CreateBuffer(device, REFRESH_BUFFERUSAGE_VERTEX_BIT, sizeof(Vertex) * 3);
	REFRESH_SetBufferData(device, vertexBuffer, 0, vertices, sizeof(Vertex) * 3);

	uint64_t* offsets = SDL_malloc(sizeof(uint64_t));
	offsets[0] = 0;

	/* Uniforms struct */

	RaymarchUniforms raymarchUniforms;
	raymarchUniforms.time = 0;
	raymarchUniforms.padding = 0;
	raymarchUniforms.resolutionX = (float)windowWidth;
	raymarchUniforms.resolutionY = (float)windowHeight;

	/* Define RenderPass */

	REFRESH_ColorTargetDescription mainColorTargetDescription;
	mainColorTargetDescription.format = REFRESH_SURFACEFORMAT_R8G8B8A8;
	mainColorTargetDescription.loadOp = REFRESH_LOADOP_CLEAR;
	mainColorTargetDescription.storeOp = REFRESH_STOREOP_STORE;
	mainColorTargetDescription.multisampleCount = REFRESH_SAMPLECOUNT_1;

	REFRESH_DepthStencilTargetDescription mainDepthStencilTargetDescription;
	mainDepthStencilTargetDescription.depthFormat = REFRESH_DEPTHFORMAT_D32_SFLOAT_S8_UINT;
	mainDepthStencilTargetDescription.loadOp = REFRESH_LOADOP_CLEAR;
	mainDepthStencilTargetDescription.storeOp = REFRESH_STOREOP_DONT_CARE;
	mainDepthStencilTargetDescription.stencilLoadOp = REFRESH_LOADOP_DONT_CARE;
	mainDepthStencilTargetDescription.stencilStoreOp = REFRESH_STOREOP_DONT_CARE;

	REFRESH_RenderPassCreateInfo mainRenderPassCreateInfo;
	mainRenderPassCreateInfo.colorTargetCount = 1;
	mainRenderPassCreateInfo.colorTargetDescriptions = &mainColorTargetDescription;
	mainRenderPassCreateInfo.depthTargetDescription = &mainDepthStencilTargetDescription;

	REFRESH_RenderPass *mainRenderPass = REFRESH_CreateRenderPass(device, &mainRenderPassCreateInfo);

	/* Define ColorTarget */

	REFRESH_Texture *mainColorTargetTexture = REFRESH_CreateTexture2D(
		device,
		REFRESH_SURFACEFORMAT_R8G8B8A8,
		windowWidth,
		windowHeight,
		1,
		REFRESH_TEXTUREUSAGE_COLOR_TARGET_BIT
	);

	REFRESH_TextureSlice mainColorTargetTextureSlice;
	mainColorTargetTextureSlice.texture = mainColorTargetTexture;
	mainColorTargetTextureSlice.rectangle.x = 0;
	mainColorTargetTextureSlice.rectangle.y = 0;
	mainColorTargetTextureSlice.rectangle.w = windowWidth;
	mainColorTargetTextureSlice.rectangle.h = windowHeight;
	mainColorTargetTextureSlice.depth = 0;
	mainColorTargetTextureSlice.layer = 0;
	mainColorTargetTextureSlice.level = 0;

	REFRESH_ColorTarget *mainColorTarget = REFRESH_CreateColorTarget(
		device,
		REFRESH_SAMPLECOUNT_1,
		&mainColorTargetTextureSlice
	);

	REFRESH_DepthStencilTarget *mainDepthStencilTarget = REFRESH_CreateDepthStencilTarget(
		device,
		windowWidth,
		windowHeight,
		REFRESH_DEPTHFORMAT_D32_SFLOAT_S8_UINT
	);

	/* Define Framebuffer */

	REFRESH_FramebufferCreateInfo framebufferCreateInfo;
	framebufferCreateInfo.width = 1280;
	framebufferCreateInfo.height = 720;
	framebufferCreateInfo.colorTargetCount = 1;
	framebufferCreateInfo.pColorTargets = &mainColorTarget;
	framebufferCreateInfo.pDepthStencilTarget = mainDepthStencilTarget;
	framebufferCreateInfo.renderPass = mainRenderPass;

	REFRESH_Framebuffer *mainFramebuffer = REFRESH_CreateFramebuffer(device, &framebufferCreateInfo);

	/* Define pipeline */
	REFRESH_ColorTargetBlendState renderTargetBlendState;
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

	REFRESH_ColorBlendState colorBlendState;
	colorBlendState.logicOpEnable = 0;
	colorBlendState.logicOp = REFRESH_LOGICOP_NO_OP;
	colorBlendState.blendConstants[0] = 0.0f;
	colorBlendState.blendConstants[1] = 0.0f;
	colorBlendState.blendConstants[2] = 0.0f;
	colorBlendState.blendConstants[3] = 0.0f;
	colorBlendState.blendStateCount = 1;
	colorBlendState.blendStates = &renderTargetBlendState;

	REFRESH_DepthStencilState depthStencilState;
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

	REFRESH_ShaderStageState vertexShaderStageState;
	vertexShaderStageState.shaderModule = passthroughVertexShaderModule;
	vertexShaderStageState.entryPointName = "main";
	vertexShaderStageState.uniformBufferSize = 0;

	REFRESH_ShaderStageState fragmentShaderStageState;
	fragmentShaderStageState.shaderModule = raymarchFragmentShaderModule;
	fragmentShaderStageState.entryPointName = "main";
	fragmentShaderStageState.uniformBufferSize = sizeof(RaymarchUniforms);

	REFRESH_MultisampleState multisampleState;
	multisampleState.multisampleCount = REFRESH_SAMPLECOUNT_1;
	multisampleState.sampleMask = 0;

	REFRESH_GraphicsPipelineLayoutCreateInfo pipelineLayoutCreateInfo;
	pipelineLayoutCreateInfo.vertexSamplerBindingCount = 0;
	pipelineLayoutCreateInfo.fragmentSamplerBindingCount = 2;

	REFRESH_RasterizerState rasterizerState;
	rasterizerState.cullMode = REFRESH_CULLMODE_BACK;
	rasterizerState.depthBiasClamp = 0;
	rasterizerState.depthBiasConstantFactor = 0;
	rasterizerState.depthBiasEnable = 0;
	rasterizerState.depthBiasSlopeFactor = 0;
	rasterizerState.depthClampEnable = 0;
	rasterizerState.fillMode = REFRESH_FILLMODE_FILL;
	rasterizerState.frontFace = REFRESH_FRONTFACE_CLOCKWISE;
	rasterizerState.lineWidth = 1.0f;

	REFRESH_TopologyState topologyState;
	topologyState.topology = REFRESH_PRIMITIVETYPE_TRIANGLELIST;

	REFRESH_VertexBinding vertexBinding;
	vertexBinding.binding = 0;
	vertexBinding.inputRate = REFRESH_VERTEXINPUTRATE_VERTEX;
	vertexBinding.stride = sizeof(Vertex);

	REFRESH_VertexAttribute *vertexAttributes = SDL_stack_alloc(REFRESH_VertexAttribute, 2);
	vertexAttributes[0].binding = 0;
	vertexAttributes[0].location = 0;
	vertexAttributes[0].format = REFRESH_VERTEXELEMENTFORMAT_VECTOR3;
	vertexAttributes[0].offset = 0;

	vertexAttributes[1].binding = 0;
	vertexAttributes[1].location = 1;
	vertexAttributes[1].format = REFRESH_VERTEXELEMENTFORMAT_VECTOR2;
	vertexAttributes[1].offset = sizeof(float) * 3;

	REFRESH_VertexInputState vertexInputState;
	vertexInputState.vertexBindings = &vertexBinding;
	vertexInputState.vertexBindingCount = 1;
	vertexInputState.vertexAttributes = vertexAttributes;
	vertexInputState.vertexAttributeCount = 2;

	REFRESH_Viewport viewport;
	viewport.x = 0;
	viewport.y = 0;
	viewport.w = (float)windowWidth;
	viewport.h = (float)windowHeight;
	viewport.minDepth = 0;
	viewport.maxDepth = 1;

	REFRESH_ViewportState viewportState;
	viewportState.viewports = &viewport;
	viewportState.viewportCount = 1;
	viewportState.scissors = &renderArea;
	viewportState.scissorCount = 1;

	REFRESH_GraphicsPipelineCreateInfo raymarchPipelineCreateInfo;
	raymarchPipelineCreateInfo.colorBlendState = colorBlendState;
	raymarchPipelineCreateInfo.depthStencilState = depthStencilState;
	raymarchPipelineCreateInfo.vertexShaderState = vertexShaderStageState;
	raymarchPipelineCreateInfo.fragmentShaderState = fragmentShaderStageState;
	raymarchPipelineCreateInfo.multisampleState = multisampleState;
	raymarchPipelineCreateInfo.pipelineLayoutCreateInfo = pipelineLayoutCreateInfo;
	raymarchPipelineCreateInfo.rasterizerState = rasterizerState;
	raymarchPipelineCreateInfo.topologyState = topologyState;
	raymarchPipelineCreateInfo.vertexInputState = vertexInputState;
	raymarchPipelineCreateInfo.viewportState = viewportState;
	raymarchPipelineCreateInfo.renderPass = mainRenderPass;

	REFRESH_GraphicsPipeline* raymarchPipeline = REFRESH_CreateGraphicsPipeline(device, &raymarchPipelineCreateInfo);

	REFRESH_Color clearColor;
	clearColor.r = 100;
	clearColor.g = 149;
	clearColor.b = 237;
	clearColor.a = 255;

	REFRESH_DepthStencilValue depthStencilClear;
	depthStencilClear.depth = 1.0f;
	depthStencilClear.stencil = 0.0f;

	/* Sampling */

	REFRESH_SamplerStateCreateInfo samplerStateCreateInfo;
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

	REFRESH_Sampler *sampler = REFRESH_CreateSampler(
		device,
		&samplerStateCreateInfo
	);

	REFRESH_Texture* sampleTextures[2];
	sampleTextures[0] = woodTexture;
	sampleTextures[1] = noiseTexture;

	REFRESH_Sampler* sampleSamplers[2];
	sampleSamplers[0] = sampler;
	sampleSamplers[1] = sampler;

	REFRESH_Rect flip;
	flip.x = 0;
	flip.y = windowHeight;
	flip.w = windowWidth;
	flip.h = -windowHeight;

	uint8_t screenshotKey = 0;
	uint8_t *screenshotPixels = SDL_malloc(sizeof(uint8_t) * windowWidth * windowHeight * 4);
	REFRESH_Buffer *screenshotBuffer = REFRESH_CreateBuffer(device, 0, windowWidth * windowHeight * 4);

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
		}

		if (updateThisLoop && !quit)
		{
			// Draw here!

			REFRESH_CommandBuffer *commandBuffer = REFRESH_AcquireCommandBuffer(device, 0);

			REFRESH_BeginRenderPass(
				device,
				commandBuffer,
				mainRenderPass,
				mainFramebuffer,
				renderArea,
				&clearColor,
				1,
				&depthStencilClear
			);

			REFRESH_BindGraphicsPipeline(
				device,
				commandBuffer,
				raymarchPipeline
			);

			raymarchUniforms.time = (float)t;

			uint32_t fragmentParamOffset = REFRESH_PushFragmentShaderParams(device, commandBuffer, &raymarchUniforms, 1);
			REFRESH_BindVertexBuffers(device, commandBuffer, 0, 1, &vertexBuffer, offsets);
			REFRESH_SetFragmentSamplers(device, commandBuffer, sampleTextures, sampleSamplers);
			REFRESH_DrawPrimitives(device, commandBuffer, 0, 1, 0, fragmentParamOffset);

			REFRESH_Clear(device, commandBuffer, &renderArea, REFRESH_CLEAROPTIONS_DEPTH | REFRESH_CLEAROPTIONS_STENCIL, NULL, 0, 0.5f, 10);
			REFRESH_EndRenderPass(device, commandBuffer);

			if (screenshotKey == 1)
			{
				SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION, "screenshot!");
				REFRESH_CopyTextureToBuffer(device, commandBuffer, &mainColorTargetTextureSlice, screenshotBuffer);
			}

			REFRESH_QueuePresent(device, commandBuffer, &mainColorTargetTextureSlice, &flip, REFRESH_FILTER_NEAREST);
			REFRESH_Submit(device, &commandBuffer, 1);

			if (screenshotKey == 1)
			{
				REFRESH_Image_SavePNG("screenshot.png", windowWidth, windowHeight, screenshotPixels);
			}
		}
	}

	SDL_free(screenshotPixels);

	REFRESH_AddDisposeColorTarget(device, mainColorTarget);
	REFRESH_AddDisposeDepthStencilTarget(device, mainDepthStencilTarget);

	REFRESH_AddDisposeTexture(device, woodTexture);
	REFRESH_AddDisposeTexture(device, noiseTexture);
	REFRESH_AddDisposeTexture(device, mainColorTargetTexture);
	REFRESH_AddDisposeSampler(device, sampler);

	REFRESH_AddDisposeBuffer(device, vertexBuffer);
	REFRESH_AddDisposeBuffer(device, screenshotBuffer);

	REFRESH_AddDisposeGraphicsPipeline(device, raymarchPipeline);

	REFRESH_AddDisposeShaderModule(device, passthroughVertexShaderModule);
	REFRESH_AddDisposeShaderModule(device, raymarchFragmentShaderModule);

	REFRESH_AddDisposeFramebuffer(device, mainFramebuffer);
	REFRESH_AddDisposeRenderPass(device, mainRenderPass);

	REFRESH_DestroyDevice(device);

	SDL_DestroyWindow(window);
	SDL_Quit();

	return 0;
}
