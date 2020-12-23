#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>

#define SDL_MAIN_HANDLED
#include <SDL.h>

#include <Refresh.h>

typedef struct Vertex
{
	float x, y, z;
	float u, v;
} Vertex;

typedef struct RaymarchUniforms
{
	float time;
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

	SDL_RWops* file = SDL_RWFromFile("passthrough_vert.spirv", "rb");
	Sint64 shaderCodeSize = SDL_RWsize(file);
	uint32_t* byteCode = SDL_malloc(sizeof(uint32_t) * shaderCodeSize);
	SDL_RWread(file, byteCode, sizeof(uint32_t), shaderCodeSize);
	SDL_RWclose(file);

	REFRESH_ShaderModuleCreateInfo passthroughVertexShaderModuleCreateInfo;
	passthroughVertexShaderModuleCreateInfo.byteCode = byteCode;
	passthroughVertexShaderModuleCreateInfo.codeSize = shaderCodeSize;

	REFRESH_ShaderModule* passthroughVertexShaderModule = REFRESH_CreateShaderModule(device, &passthroughVertexShaderModuleCreateInfo);

	file = SDL_RWFromFile("raymarch_frag.spirv", "rb");
	shaderCodeSize = SDL_RWsize(file);
	byteCode = SDL_realloc(byteCode, sizeof(uint32_t) * shaderCodeSize);
	SDL_RWread(file, byteCode, sizeof(uint32_t), shaderCodeSize);
	SDL_RWclose(file);

	REFRESH_ShaderModuleCreateInfo raymarchFragmentShaderModuleCreateInfo;
	raymarchFragmentShaderModuleCreateInfo.byteCode = byteCode;
	raymarchFragmentShaderModuleCreateInfo.codeSize = shaderCodeSize;

	REFRESH_ShaderModule* raymarchFragmentShaderModule = REFRESH_CreateShaderModule(device, &raymarchFragmentShaderModuleCreateInfo);

	SDL_free(byteCode);

	/* Define vertex buffer */

	Vertex* vertices = SDL_malloc(sizeof(Vertex) * 3);
	vertices[0].x = -1;
	vertices[0].y = -1;
	vertices[0].z = 0;
	vertices[0].u = 0;
	vertices[0].v = 0;

	vertices[1].x = 3;
	vertices[1].y = -1;
	vertices[1].z = 0;
	vertices[1].u = 1;
	vertices[1].v = 0;

	vertices[2].x = -1;
	vertices[2].y = 3;
	vertices[2].z = 0;
	vertices[2].u = 0;
	vertices[2].v = 1;

	REFRESH_Buffer* vertexBuffer = REFRESH_CreateVertexBuffer(device, sizeof(Vertex) * 3);
	REFRESH_SetVertexBufferData(device, vertexBuffer, 0, vertices, 3, sizeof(Vertex));

	uint64_t* offsets = SDL_malloc(sizeof(uint64_t));
	offsets[0] = 0;

	/* Uniforms struct */

	RaymarchUniforms raymarchUniforms;
	raymarchUniforms.time = 0;

	/* Define RenderPass */

	REFRESH_ColorTargetDescription mainColorTargetDescription;
	mainColorTargetDescription.format = REFRESH_SURFACEFORMAT_R8G8B8A8;
	mainColorTargetDescription.loadOp = REFRESH_LOADOP_CLEAR;
	mainColorTargetDescription.storeOp = REFRESH_STOREOP_STORE;
	mainColorTargetDescription.multisampleCount = REFRESH_SAMPLECOUNT_1;

	REFRESH_RenderPassCreateInfo mainRenderPassCreateInfo;
	mainRenderPassCreateInfo.colorTargetCount = 1;
	mainRenderPassCreateInfo.colorTargetDescriptions = &mainColorTargetDescription;
	mainRenderPassCreateInfo.depthTargetDescription = NULL;

	REFRESH_RenderPass *mainRenderPass = REFRESH_CreateRenderPass(device, &mainRenderPassCreateInfo);

	/* Define ColorTarget */

	REFRESH_Texture *mainColorTargetTexture = REFRESH_CreateTexture2D(device, REFRESH_SURFACEFORMAT_R8G8B8A8, windowWidth, windowHeight, 1, 1);

	REFRESH_TextureSlice mainColorTargetTextureSlice;
	mainColorTargetTextureSlice.texture = mainColorTargetTexture;
	mainColorTargetTextureSlice.layer = 0;

	REFRESH_ColorTarget *mainColorTarget = REFRESH_CreateColorTarget(device, REFRESH_SAMPLECOUNT_1, &mainColorTargetTextureSlice);

	/* Define Framebuffer */

	REFRESH_FramebufferCreateInfo framebufferCreateInfo;
	framebufferCreateInfo.width = 1280;
	framebufferCreateInfo.height = 720;
	framebufferCreateInfo.colorTargetCount = 1;
	framebufferCreateInfo.pColorTargets = &mainColorTarget;
	framebufferCreateInfo.pDepthStencilTarget = NULL;
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
	colorBlendState.blendOpEnable = 0;
	colorBlendState.blendConstants[0] = 0.0f;
	colorBlendState.blendConstants[1] = 0.0f;
	colorBlendState.blendConstants[2] = 0.0f;
	colorBlendState.blendConstants[3] = 0.0f;
	colorBlendState.blendStateCount = 1;
	colorBlendState.blendStates = &renderTargetBlendState;
	colorBlendState.logicOp = REFRESH_LOGICOP_NO_OP;

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

	REFRESH_PipelineLayoutCreateInfo pipelineLayoutCreateInfo;
	pipelineLayoutCreateInfo.vertexSamplerBindings = NULL;
	pipelineLayoutCreateInfo.vertexSamplerBindingCount = 0;
	pipelineLayoutCreateInfo.fragmentSamplerBindings = NULL;
	pipelineLayoutCreateInfo.fragmentSamplerBindingCount = 0;

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
		}

		if (updateThisLoop && !quit)
		{
			// Draw here!

			REFRESH_BeginRenderPass(
				device,
				mainRenderPass,
				mainFramebuffer,
				renderArea,
				&clearColor,
				1,
				NULL
			);

			REFRESH_BindGraphicsPipeline(
				device,
				raymarchPipeline
			);

			raymarchUniforms.time = t;

			REFRESH_PushFragmentShaderParams(device, &raymarchUniforms, 1);
			REFRESH_BindVertexBuffers(device, 0, 1, &vertexBuffer, offsets);
			REFRESH_DrawPrimitives(device, 0, 1);

			REFRESH_EndRenderPass(device);

			REFRESH_QueuePresent(device, &mainColorTargetTextureSlice, NULL, NULL);
			REFRESH_Submit(device);
		}
	}

	// todo: free vertex buffers (and everything)

	REFRESH_DestroyDevice(device);

	SDL_DestroyWindow(window);
	SDL_Quit();

	return 0;
}
