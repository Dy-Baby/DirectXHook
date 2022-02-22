#pragma once

#include <d3d11.h>
#include <vector>
#include <fstream>
#include <WICTextureLoader.h>
#include <comdef.h>
#include <SpriteBatch.h>
#include <SpriteFont.h>
#include <wrl/client.h>
#include <chrono>
#include <string>

#undef DrawText

namespace OF
{
	struct Box
	{
		int x = 0;
		int y = 0;
		float z = 0; // Used for layering
		int width = 0;
		int height = 0;
		Box* parentBox = nullptr;
		bool pressed = false; // Whether or not the box is currently being pressed (left mouse button held down)
		bool clicked = false; // Whether or not the box has been clicked this frame (left mouse button pressed and then released)
		bool hover = false; // Whether or not the cursor is currently hovering over this box
		bool draggable = true;
		bool visible = false; // Managed by the framework, do not change 
	};

	static const char* ofPrintPrefix = "OverlayFramework >";

	static HWND ofWindow = 0;
	static int ofWindowWidth = 0;
	static int ofWindowHeight = 0;
	static std::vector<Box*> ofBoxes = std::vector<Box*>();
	static std::vector<int> ofBoxOrder = std::vector<int>();
	static int ofMouseX = 0, ofMouseY = 0;
	static int ofDeltaMouseX = 0, ofDeltaMouseY = 0;
	static bool ofMousePressed = false;
	static Box* ofClickedBox = nullptr;

	static Microsoft::WRL::ComPtr<ID3D11Device> ofDevice = nullptr;
	static std::shared_ptr<DirectX::SpriteBatch> ofSpriteBatch = nullptr;
	static std::vector<Microsoft::WRL::ComPtr<ID3D11ShaderResourceView>> ofTextures = std::vector<Microsoft::WRL::ComPtr<ID3D11ShaderResourceView>>();
	static bool ofFailedToLoadBlank = false;
	static std::vector<std::shared_ptr<DirectX::SpriteFont>> ofFonts = std::vector<std::shared_ptr<DirectX::SpriteFont>>();
	static std::shared_ptr<DirectX::SpriteFont> ofActiveFont = nullptr;

	// Gives the framework the required DirectX objects to draw
	inline void InitFramework(Microsoft::WRL::ComPtr<ID3D11Device> device, std::shared_ptr<DirectX::SpriteBatch> spriteBatch, HWND window)
	{
		printf("%s Initialized\n", ofPrintPrefix);
		ofDevice = device;
		ofSpriteBatch = spriteBatch;
		ofWindow = window;
		RECT hwndRect;
		GetClientRect(ofWindow, &hwndRect);
		ofWindowWidth = hwndRect.right - hwndRect.left;
		ofWindowHeight = hwndRect.bottom - hwndRect.top;
	}

	inline int MapIntToRange(int number, int inputStart, int inputEnd, int outputStart, int outputEnd)
	{
		return outputStart + (outputEnd - outputStart) * (number - inputStart) / (inputEnd - inputStart);
	}

	inline float MapFloatToRange(float number, float inputStart, float inputEnd, float outputStart, float outputEnd)
	{
		return outputStart + (outputEnd - outputStart) * (number - inputStart) / (inputEnd - inputStart);
	}

	inline int LoadTexture(std::string filepath)
	{
		if (ofDevice == nullptr)
		{
			printf("%s Could not load texture, ofDevice is nullptr! Run InitFramework before attempting to load textures!\n", ofPrintPrefix);
			return -1;
		}

		// Load the blank texture to index 0 if the texture is not loaded already.
		// Stops any other texture loading unless the blank texture is loaded successfully.
		if (ofTextures.size() == 0 && filepath != "blank") {
			if (LoadTexture("blank") != 0) return -1;
		}
		else if (filepath == "blank")
		{
			filepath = "hook_textures\\blank.jpg";
		}

		printf("%s Loading texture: %s\n", ofPrintPrefix, filepath.c_str());

		std::wstring wideString(filepath.length(), ' ');
		std::copy(filepath.begin(), filepath.end(), wideString.begin());
		std::fstream file = std::fstream(filepath);
		if (file.fail())
		{
			printf("%s Texture loading failed, file was not found at: %s\n", ofPrintPrefix, filepath.c_str());
			file.close();
			return -1;
		}
		file.close();

		Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> texture = nullptr;
		HRESULT texResult = DirectX::CreateWICTextureFromFile(ofDevice.Get(), wideString.c_str(), nullptr, texture.GetAddressOf());

		_com_error texErr(texResult);
		printf("%s Texture HRESULT: %s\n", ofPrintPrefix, texErr.ErrorMessage());
		if (FAILED(texResult))
		{
			printf("%s Texture loading failed: %s\n", ofPrintPrefix, filepath.c_str());
			return -1;
		}

		ofTextures.push_back(texture);

		return ofTextures.size() - 1;
	}

	inline int LoadFont(std::string filepath)
	{
		if (ofDevice == nullptr)
		{
			printf("%s Could not load font, ofDevice is nullptr! Run InitFramework before attempting to load fonts!\n", ofPrintPrefix);
			return -1;
		}

		printf("%s Loading font: %s\n", ofPrintPrefix, filepath.c_str());

		std::fstream file = std::fstream(filepath);
		std::wstring wideString(filepath.length(), ' ');
		std::copy(filepath.begin(), filepath.end(), wideString.begin());
		if (file.fail())
		{
			printf("%s Font loading failed: %s\n", ofPrintPrefix, filepath.c_str());
			file.close();
			return -1;
		}

		file.close();

		printf("%s Font was loaded successfully\n", ofPrintPrefix);
		ofFonts.push_back(std::make_shared<DirectX::SpriteFont>(ofDevice.Get(), wideString.c_str()));

		return ofFonts.size() - 1;
	}

	inline void SetFont(int font)
	{
		if (font > ofFonts.size() - 1 || font < 0)
		{
			printf("%s Attempted to set invalid font!\n", ofPrintPrefix);
			return;
		}

		ofActiveFont = ofFonts[font];
	}

	// Place the given box on top of all other boxes
	inline void PlaceOnTop(Box* boxOnTop)
	{
		int boxIndex = 0;
		for (int i = 0; i < ofBoxes.size(); i++)
		{
			if (ofBoxes[i] == boxOnTop) boxIndex = i;
		}
		ofBoxOrder.push_back(boxIndex);

		for (int i = 0; i < ofBoxOrder.size() - 1; i++)
		{
			if (ofBoxes[ofBoxOrder[i]] == ofBoxes[ofBoxOrder.back()])
			{
				ofBoxOrder.erase(ofBoxOrder.begin() + i);
			}
		}

		for (float i = 0; i < ofBoxOrder.size(); i++)
		{
			ofBoxes[ofBoxOrder[i]]->z = 1.0f / (1 + (i / 1000));
		}
	}

	inline POINT GetAbsolutePosition(Box* box)
	{
		if (box == nullptr)
		{
			return { 0, 0 };
		}

		POINT absolutePosition = { box->x, box->y };
		Box* parentBox = box->parentBox;
		while (parentBox != nullptr)
		{
			if (parentBox->parentBox == box)
			{
				break;
			}

			absolutePosition.x += parentBox->x;
			absolutePosition.y += parentBox->y;

			parentBox = parentBox->parentBox;
		}

		return absolutePosition;
	}

	inline Box* CreateBox(Box* parentBox, int x, int y, int width, int height)
	{
		Box* box = new Box;
		box->x = x;
		box->y = y;
		box->width = width;
		box->height = height;
		box->parentBox = parentBox;

		if (parentBox != nullptr)
		{
			box->draggable = false;
		}

		ofBoxes.push_back(box);
		PlaceOnTop(box);
		return ofBoxes.back();
	}

	inline Box* CreateBox(int x, int y, int width, int height)
	{
		return CreateBox(nullptr, x, y, width, height);
	}
	
	inline void _DrawBox(Box* box, DirectX::XMVECTOR color, int textureID)
	{
		if (box == nullptr) 
		{
			printf("%s Attempted to render a nullptr Box!\n", ofPrintPrefix);
			return;
		}

		if (ofSpriteBatch == nullptr)
		{
			printf("%s Attempted to render with ofSpriteBatch as nullptr! Run InitFramework before attempting to draw!\n", ofPrintPrefix);
			return;
		}

		if (ofTextures.size() < 1) 
		{
			if (ofFailedToLoadBlank == false && LoadTexture("blank") != 0) {
				ofFailedToLoadBlank = true;
			}
			return;
		}

		if (textureID < 0 || textureID > ofTextures.size() - 1) 
		{
			printf("%s '%i' is an invalid texture ID!\n", ofPrintPrefix, textureID);
			return;
		}
	
		POINT position = GetAbsolutePosition(box);

		RECT rect;
		rect.top = position.y;
		rect.left = position.x;
		rect.bottom = position.y + box->height;
		rect.right = position.x + box->width;

		box->visible = true;
		ofSpriteBatch->Draw(ofTextures[textureID].Get(), rect, nullptr, color, 0.0f, DirectX::XMFLOAT2(0.0f, 0.0f), DirectX::SpriteEffects_None, box->z);
	}

	inline void DrawBox(Box* box, int textureID)
	{
		_DrawBox(box, { 1.0f, 1.0f, 1.0f, 1.0f }, textureID);
	}

	inline void DrawBox(Box* box, int r, int g, int b, int a)
	{
		float _r = MapFloatToRange((float)r, 0.0f, 255.0f, 0.0f, 1.0f);
		float _g = MapFloatToRange((float)g, 0.0f, 255.0f, 0.0f, 1.0f);
		float _b = MapFloatToRange((float)b, 0.0f, 255.0f, 0.0f, 1.0f);
		float _a = MapFloatToRange((float)a, 0.0f, 255.0f, 0.0f, 1.0f);
		_DrawBox(box, { _r, _g, _b, _a }, 0);
	}

	inline void DrawText(Box* box, std::string text, int offsetX = 0, int offsetY = 0, float scale = 1.0f,
		int r = 255, int g = 255, int b = 255, int a = 255, float rotation = 0.0f)
	{
		if (ofActiveFont == nullptr)
		{
			printf("%s Attempted to render text with an invalid font, make sure to run SetFont first!\n", ofPrintPrefix);
			return;
		}

		POINT position = GetAbsolutePosition(box);

		DirectX::XMFLOAT2 textPos = DirectX::XMFLOAT2
		(
			position.x + offsetX,
			position.y + offsetY
		);

		float _r = MapFloatToRange((float)r, 0.0f, 255.0f, 0.0f, 1.0f);
		float _g = MapFloatToRange((float)g, 0.0f, 255.0f, 0.0f, 1.0f);
		float _b = MapFloatToRange((float)b, 0.0f, 255.0f, 0.0f, 1.0f);
		float _a = MapFloatToRange((float)a, 0.0f, 255.0f, 0.0f, 1.0f);

		ofActiveFont->DrawString(ofSpriteBatch.get(), text.c_str(), textPos, { _r, _g, _b, _a }, rotation, { 0.0f, 0.0f }, scale, DirectX::SpriteEffects_None, box->z);
	}

	inline bool IsCursorInsideBox(POINT cursorPos, Box* box)
	{
		POINT position = GetAbsolutePosition(box);
		POINT boxSize = { box->width, box->height };

		if (cursorPos.x < (position.x + boxSize.x) && cursorPos.x > position.x)
		{
			if (cursorPos.y < (position.y + boxSize.y) && cursorPos.y > position.y)
			{
				return true;
			}
		}

		return false;
	}

	inline bool CheckHotkey(unsigned char key, unsigned char modifier = ' ')
	{
		static std::vector<unsigned char> notReleasedKeys;

		if (ofWindow != GetForegroundWindow())
		{
			return false;
		}

		bool keyPressed = GetAsyncKeyState(key) & 0x8000;
		bool modifierPressed = GetAsyncKeyState(modifier) & 0x8000;

		if (key == ' ')
		{
			return modifierPressed;
		}

		auto iterator = std::find(notReleasedKeys.begin(), notReleasedKeys.end(), key);
		bool keyNotReleased = iterator != notReleasedKeys.end();

		if (keyPressed && keyNotReleased)
		{
			return false;
		} 
		
		if(!keyPressed)
		{
			if (keyNotReleased)
			{
				notReleasedKeys.erase(iterator);
			}
			return false;
		}

		if (modifier != ' ' && !modifierPressed)
		{
			return false;
		}

		notReleasedKeys.push_back(key);
		return true;
	}

	inline void CheckMouseEvents()
	{
		if (ofWindow == GetForegroundWindow())
		{
			POINT cursorPos;
			GetCursorPos(&cursorPos);
			ScreenToClient(ofWindow, &cursorPos);

			ofDeltaMouseX = ofMouseX;
			ofDeltaMouseY = ofMouseY;
			ofMouseX = cursorPos.x;
			ofMouseY = cursorPos.y;
			ofDeltaMouseX = ofDeltaMouseX - ofMouseX;
			ofDeltaMouseY = ofDeltaMouseY - ofMouseY;

			// Reset last frame's clicked box to not count as clicked anymore
			if (ofClickedBox != nullptr)
			{
				if (ofClickedBox->clicked)
				{
					ofClickedBox->clicked = false;
					ofClickedBox = nullptr;
				}
			}

			Box* topMostBox = nullptr;
			for (int i = 0; i < ofBoxes.size(); i++)
			{
				Box* box = ofBoxes[i];
				box->hover = false;

				if (!box->visible)
				{
					continue;
				}

				if (IsCursorInsideBox(cursorPos, box))
				{
					if (topMostBox == nullptr || box->z < topMostBox->z)
					{
						topMostBox = box;
					}
				}
			}

			if (topMostBox != nullptr)
			{
				topMostBox->hover = true;
			}

			if (GetAsyncKeyState(VK_LBUTTON) & 0x8000)
			{
				if (topMostBox != nullptr && !ofMousePressed)
				{
					ofMousePressed = true;
					ofClickedBox = topMostBox;
					ofClickedBox->pressed = true;
				}

				if (ofClickedBox != nullptr && ofClickedBox->draggable)
				{
					ofClickedBox->x -= ofDeltaMouseX;
					ofClickedBox->y -= ofDeltaMouseY;
				}
			}
			else
			{
				if (ofClickedBox != nullptr && IsCursorInsideBox(cursorPos, ofClickedBox))
				{
					if (ofClickedBox->parentBox != nullptr)
					{
						PlaceOnTop(ofClickedBox->parentBox);

						for (int i = 0; i < ofBoxes.size(); i++)
						{
							if (ofClickedBox->parentBox == ofBoxes[i]->parentBox)
							{
								PlaceOnTop(ofBoxes[i]);
							}
						}
					}

					PlaceOnTop(ofClickedBox);

					for (int i = 0; i < ofBoxes.size(); i++)
					{
						if (ofBoxes[i]->parentBox == ofClickedBox)
						{
							PlaceOnTop(ofBoxes[i]);
						}
					}

					ofClickedBox->pressed = false;
					ofClickedBox->clicked = true;
				}

				ofMousePressed = false;
			}
		}

		for (auto box : ofBoxes)
		{
			box->visible = false;
		}
	}
};