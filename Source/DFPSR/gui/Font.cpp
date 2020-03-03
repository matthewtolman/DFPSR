// zlib open source license
//
// Copyright (c) 2018 to 2019 David Forsgren Piuva
// 
// This software is provided 'as-is', without any express or implied
// warranty. In no event will the authors be held liable for any damages
// arising from the use of this software.
// 
// Permission is granted to anyone to use this software for any purpose,
// including commercial applications, and to alter it and redistribute it
// freely, subject to the following restrictions:
// 
//    1. The origin of this software must not be misrepresented; you must not
//    claim that you wrote the original software. If you use this software
//    in a product, an acknowledgment in the product documentation would be
//    appreciated but is not required.
// 
//    2. Altered source versions must be plainly marked as such, and must not be
//    misrepresented as being the original software.
// 
//    3. This notice may not be removed or altered from any source
//    distribution.

#include <stdint.h>
#include "Font.h"
#include "defaultFont.h"
#include "../api/imageAPI.h"
#include "../api/drawAPI.h"

using namespace dsr;

std::shared_ptr<RasterFont> defaultFont = RasterFont::createLatinOne(U"UbuntuMono", image_fromAscii(defaultFontAscii));;

std::shared_ptr<RasterFont> dsr::font_getDefault() {
	return defaultFont;
}


RasterCharacter::RasterCharacter(const ImageU8& image, DsrChar unicodeValue, int32_t offsetY)
: image(image), unicodeValue(unicodeValue), width(image_getWidth(image)), offsetY(offsetY) {}

RasterFont::RasterFont(const String& name, int32_t size, int32_t spacing, int32_t spaceWidth)
 : name(name), size(size), spacing(spacing), spaceWidth(spaceWidth), tabWidth(spaceWidth * 4) {
	for (int i = 0; i < 65536; i++) {
		this->indices[i] = -1;
	}
}

RasterFont::~RasterFont() {}

std::shared_ptr<RasterFont> RasterFont::createLatinOne(const String& name, const ImageU8& atlas) {
	int32_t size = image_getHeight(atlas) / 16;
	std::shared_ptr<RasterFont> result = std::make_shared<RasterFont>(name, size, size / 16, size / 2);
	result->registerLatinOne16x16(atlas);
	return result;
}

void RasterFont::registerCharacter(const ImageU8& image, DsrChar unicodeValue, int32_t offsetY) {
	if (this->indices[unicodeValue] == -1) {
		// Add the unicode character
		this->characters.pushConstruct(image, unicodeValue, offsetY);
		// Add to latin-1 table if inside the range
		if (unicodeValue < 65536) {
			this->indices[unicodeValue] = this->characters.length() - 1;
		}
	}
}

static IRect getCharacterBound(const ImageU8& image, const IRect& searchRegion) {
	// Inclusive intervals for speed
	int32_t minX = searchRegion.right();
	int32_t maxX = searchRegion.left();
	int32_t minY = searchRegion.bottom();
	int32_t maxY = searchRegion.top();
	for (int y = searchRegion.top(); y < searchRegion.bottom(); y++) {
		for (int x = searchRegion.left(); x < searchRegion.right(); x++) {
			if (image_readPixel_border(image, x, y)) {
				if (x < minX) minX = x;
				if (x > maxX) maxX = x;
				if (y < minY) minY = y;
				if (y > maxY) maxY = y;
			}
		}
	}
	// Convert to width and height
	return IRect(minX, minY, (maxX + 1) - minX, (maxY + 1) - minY);
}

// Call after construction to register up to 256 characters in a 16x16 grid from the atlas
void RasterFont::registerLatinOne16x16(const ImageU8& atlas) {
	int32_t charWidth = image_getWidth(atlas) / 16;
	int32_t charHeight = image_getWidth(atlas) / 16;
	for (int y = 0; y < 16; y++) {
		for (int x = 0; x < 16; x++) {
			IRect searchRegion = IRect(x * charWidth, y * charHeight, charWidth, charHeight);
			IRect croppedRegion = getCharacterBound(atlas, searchRegion);
			if (croppedRegion.hasArea()) {
				int32_t offsetY = croppedRegion.top() - searchRegion.top();
				ImageU8 fullImage = image_getSubImage(atlas, croppedRegion);
				this->registerCharacter(fullImage, y * 16 + x, offsetY);
			}
		}
	}
}

int32_t RasterFont::getCharacterWidth(DsrChar unicodeValue) const {
	int32_t index = this->indices[unicodeValue];
	if (index > -1) {
		return this->characters[index].width + this->spacing;
	} else {
		return spaceWidth;
	}
}

// Prints a character and returns the horizontal stride in pixels
int32_t RasterFont::printCharacter(ImageRgbaU8& target, DsrChar unicodeValue, const IVector2D& location, const ColorRgbaI32& color) const {
	if (unicodeValue < 65536) {
		int32_t index = this->indices[unicodeValue];
		if (index > -1) {
			const RasterCharacter *source = &(this->characters[index]);
			draw_silhouette(target, source->image, color, location.x, location.y + source->offsetY);
		}
		return this->getCharacterWidth(unicodeValue);
	} else {
		// TODO: Look up characters outside of the 16-bit range from a sparse data structure
		return 0;
	}
}

void RasterFont::printLine(ImageRgbaU8& target, const ReadableString& content, const IVector2D& location, const ColorRgbaI32& color) const {
	IVector2D currentLocation = location;
	for (int i = 0; i < (int)(content.length()); i++) {
		DsrChar code = (DsrChar)(content[i]);
		if (code == 9) { // Tab
			// TODO: Jump to the next tab-stop in pixels relative to a tab line given from the caller
			currentLocation.x += this->tabWidth;
		} else {
			// TODO: Would right to left printing of Arabic text be too advanced to have in the core framework?
			currentLocation.x += this->printCharacter(target, code, currentLocation, color);
		}
	}
}

int32_t RasterFont::getLineWidth(const ReadableString& content) const {
	int32_t result = 0;
	for (int i = 0; i < content.length(); i++) {
		DsrChar code = content[i];
		if (code == 9) { // Tab
			// TODO: Jump to the next tab-stop in pixels relative to a tab line given from the caller
			result += this->tabWidth;
		} else {
			result += this->getCharacterWidth(code);
		}
	}
	return result;
}
