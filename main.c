#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>

#define SDL_MAIN_HANDLED
#include <SDL.h>

#include <Refresh.h>

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
	presentationParameters.presentMode = REFRESH_PRESENTMODE_MAILBOX;

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

	/* state */

	/* Define RenderPass */
	REFRESH_ColorTargetDescription doNothingColorTargetDescription;
	doNothingColorTargetDescription.format = REFRESH_SURFACEFORMAT_R8G8B8A8;
	doNothingColorTargetDescription.loadOp = REFRESH_LOADOP_CLEAR;
	doNothingColorTargetDescription.storeOp = REFRESH_STOREOP_STORE;
	doNothingColorTargetDescription.multisampleCount = REFRESH_SAMPLECOUNT_1;

	REFRESH_RenderPassCreateInfo doNothingPassCreateInfo;
	doNothingPassCreateInfo.colorTargetCount = 1;
	doNothingPassCreateInfo.colorTargetDescriptions = &doNothingColorTargetDescription;
	doNothingPassCreateInfo.depthTargetDescription = NULL;

	REFRESH_RenderPass *doNothingRenderPass = REFRESH_CreateRenderPass(device, &doNothingPassCreateInfo);

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
	framebufferCreateInfo.pDepthTarget = NULL;
	framebufferCreateInfo.renderPass = doNothingRenderPass;

	REFRESH_Framebuffer *mainFramebuffer = REFRESH_CreateFramebuffer(device, &framebufferCreateInfo);

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
				doNothingRenderPass,
				mainFramebuffer,
				renderArea,
				&clearColor,
				1,
				NULL
			);

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
