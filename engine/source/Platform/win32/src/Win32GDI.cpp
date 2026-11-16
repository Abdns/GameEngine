struct win32_window_dimensions
{
    int Width;
    int Height;
};

win32_offscreen_buffer GlobalBackBuffer;

win32_window_dimensions GetWindowDimension(HWND Window)
{
	win32_window_dimensions Result;
	RECT ClientRect;

	GetClientRect(Window, &ClientRect);

	Result.Width = ClientRect.right - ClientRect.left;
	Result.Height = ClientRect.bottom - ClientRect.top;

	return(Result);
}

void Win32DisplayBufferInWindow(win32_offscreen_buffer* Buffer, HDC DeviceContext, int WindowWidth, int WindowHeight)
{

	int OffsetX = 0;
	int OffsetY = 0;
	int DestWidth  = WindowWidth;
	int DestHeight = WindowHeight;

	if (Buffer->Width > 0 && Buffer->Height > 0 && WindowWidth > 0 && WindowHeight > 0)
	{
		real32 BufferAspect = (real32)Buffer->Width / (real32)Buffer->Height;
		real32 WindowAspect = (real32)WindowWidth   / (real32)WindowHeight;

		if (WindowAspect > BufferAspect)
		{
			DestWidth = (int)(WindowHeight * BufferAspect);
			OffsetX   = (WindowWidth - DestWidth) / 2;
		}
		else
		{
			DestHeight = (int)(WindowWidth / BufferAspect);
			OffsetY    = (WindowHeight - DestHeight) / 2;
		}
	}

	PatBlt(DeviceContext, 0,                  0,                   WindowWidth, OffsetY,                BLACKNESS);
	PatBlt(DeviceContext, 0,                  OffsetY + DestHeight, WindowWidth, WindowHeight,           BLACKNESS);
	PatBlt(DeviceContext, 0,                  OffsetY,             OffsetX,     DestHeight,             BLACKNESS);
	PatBlt(DeviceContext, OffsetX + DestWidth, OffsetY,             WindowWidth, DestHeight,             BLACKNESS);

	StretchDIBits(DeviceContext,
	              OffsetX, OffsetY, DestWidth, DestHeight,
	              0, 0, Buffer->Width, Buffer->Height,
	              Buffer->Memory, &Buffer->Info, DIB_RGB_COLORS, SRCCOPY);
}

void Win32DisplayFrame(HWND Window, HDC DeviceContext)
{
	win32_window_dimensions Dim = GetWindowDimension(Window);
	Win32DisplayBufferInWindow(&GlobalBackBuffer, DeviceContext, Dim.Width, Dim.Height);
}

void Win32ResizeDIB(win32_offscreen_buffer* Buffer, int Width, int Height)
{
	if (Buffer->Memory)
	{
		VirtualFree(Buffer->Memory, 0, MEM_RELEASE);
	}

	Buffer->Width = Width;
	Buffer->Height = Height;
	Buffer->BytesPerPixel = 4;

	Buffer->Info.bmiHeader.biSize = sizeof(Buffer->Info.bmiHeader);
	Buffer->Info.bmiHeader.biWidth = Buffer->Width;
	Buffer->Info.bmiHeader.biHeight = -Buffer->Height;
	Buffer->Info.bmiHeader.biPlanes = 1;
	Buffer->Info.bmiHeader.biBitCount = 32;
	Buffer->Info.bmiHeader.biCompression = BI_RGB;

	int BitmapMemorySize = (Buffer->Width * Buffer->Height) * Buffer->BytesPerPixel;
	Buffer->Memory = VirtualAlloc(0, BitmapMemorySize, MEM_COMMIT, PAGE_READWRITE);

	Buffer->Pitch = Width * Buffer->BytesPerPixel;
}
