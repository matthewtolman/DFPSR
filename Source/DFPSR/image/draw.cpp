﻿// zlib open source license
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

#include "../base/simdExtra.h"
#include "draw.h"
#include "internal/imageInternal.h"
#include "../math/scalar.h"
#include <limits>

using namespace dsr;

// -------------------------------- Drawing shapes --------------------------------

template <typename COLOR_TYPE>
static inline void drawSolidRectangleAssign(ImageImpl &target, int left, int top, int right, int bottom, COLOR_TYPE color) {
	int leftBound = std::max(0, left);
	int topBound = std::max(0, top);
	int rightBound = std::min(right, target.width);
	int bottomBound = std::min(bottom, target.height);
	int stride = target.stride;
	SafePointer<COLOR_TYPE> rowData = imageInternal::getSafeData<COLOR_TYPE>(target, topBound);
	rowData += leftBound;
	for (int y = topBound; y < bottomBound; y++) {
		SafePointer<COLOR_TYPE> pixelData = rowData;
		for (int x = leftBound; x < rightBound; x++) {
			pixelData.get() = color;
			pixelData += 1;
		}
		rowData.increaseBytes(stride);
	}
}

template <typename COLOR_TYPE>
static inline void drawSolidRectangleMemset(ImageImpl &target, int left, int top, int right, int bottom, uint8_t uniformByte) {
	int leftBound = std::max(0, left);
	int topBound = std::max(0, top);
	int rightBound = std::min(right, target.width);
	int bottomBound = std::min(bottom, target.height);
	if (rightBound > leftBound && bottomBound > topBound) {
		int stride = target.stride;
		SafePointer<COLOR_TYPE> rowData = imageInternal::getSafeData<COLOR_TYPE>(target, topBound);
		rowData += leftBound;
		int filledWidth = rightBound - leftBound;
		int rowSize = filledWidth * sizeof(COLOR_TYPE);
		int rowCount = bottomBound - topBound;
		if (!target.isSubImage && filledWidth == target.width) {
			// Write over any padding for parent images owning the whole buffer.
			// Including parent images with sub-images using the same data
			//   because no child image may display the parent-image's padding bytes.
			safeMemorySet(rowData, uniformByte, (stride * (rowCount - 1)) + rowSize);
		} else if (rowSize == stride) {
			// When the filled row stretches all the way from left to right in the main allocation
			//   there's no unseen pixels being overwritten in other images sharing the buffer.
			// This case handles sub-images that uses the full width of
			//   the parent image which doesn't have any padding.
			safeMemorySet(rowData, uniformByte, rowSize * rowCount);
		} else {
			// Fall back on using one memset operation per row.
			// This case is for sub-images that must preserve interleaved pixel rows belonging
			//   to other images that aren't visible and therefore not owned by this image.
			for (int y = topBound; y < bottomBound; y++) {
				safeMemorySet(rowData, uniformByte, rowSize);
				rowData.increaseBytes(stride);
			}
		}
	}
}

void dsr::imageImpl_draw_solidRectangle(ImageU8Impl& image, const IRect& bound, int color) {
	if (color < 0) { color = 0; }
	if (color > 255) { color = 255; }
	drawSolidRectangleMemset<uint8_t>(image, bound.left(), bound.top(), bound.right(), bound.bottom(), color);
}

void dsr::imageImpl_draw_solidRectangle(ImageU16Impl& image, const IRect& bound, int color) {
	if (color < 0) { color = 0; }
	if (color > 65535) { color = 65535; }
	uint16_t uColor = color;
	if (isUniformByteU16(uColor)) {
		drawSolidRectangleMemset<uint16_t>(image, bound.left(), bound.top(), bound.right(), bound.bottom(), 0);
	} else {
		drawSolidRectangleAssign<uint16_t>(image, bound.left(), bound.top(), bound.right(), bound.bottom(), uColor);
	}
}

void dsr::imageImpl_draw_solidRectangle(ImageF32Impl& image, const IRect& bound, float color) {
	if (color == 0.0f) {
		drawSolidRectangleMemset<float>(image, bound.left(), bound.top(), bound.right(), bound.bottom(), 0);
	} else {
		drawSolidRectangleAssign<float>(image, bound.left(), bound.top(), bound.right(), bound.bottom(), color);
	}
}

void dsr::imageImpl_draw_solidRectangle(ImageRgbaU8Impl& image, const IRect& bound, const ColorRgbaI32& color) {
	Color4xU8 packedColor = image.packRgba(color.saturate());
	if (packedColor.isUniformByte()) {
		drawSolidRectangleMemset<Color4xU8>(image, bound.left(), bound.top(), bound.right(), bound.bottom(), packedColor.channels[0]);
	} else {
		drawSolidRectangleAssign<Color4xU8>(image, bound.left(), bound.top(), bound.right(), bound.bottom(), packedColor);
	}
}

template <typename IMAGE_TYPE, typename COLOR_TYPE>
inline void drawLineSuper(IMAGE_TYPE &target, int x1, int y1, int x2, int y2, COLOR_TYPE color) {
	if (y1 == y2) {
		// Sideways
		int left = std::min(x1, x2);
		int right = std::max(x1, x2);
		for (int x = left; x <= right; x++) {
			IMAGE_TYPE::writePixel(target, x, y1, color);
		}
	} else if (x1 == x2) {
		// Down
		int top = std::min(y1, y2);
		int bottom = std::max(y1, y2);
		for (int y = top; y <= bottom; y++) {
			IMAGE_TYPE::writePixel(target, x1, y, color);
		}
	} else {
		if (std::abs(y2 - y1) >= std::abs(x2 - x1)) {
			if (y2 < y1) {
				swap(x1, x2);
				swap(y1, y2);
			}
			assert(y2 > y1);
			if (x2 > x1) {
				// Down right
				int x = x1;
				int y = y1;
				int tilt = (x2 - x1) * 2;
				int maxError = y2 - y1;
				int error = 0;
				while (y <= y2) {
					IMAGE_TYPE::writePixel(target, x, y, color);
					error += tilt;
					if (error >= maxError) {
						x++;
						error -= maxError * 2;
					}
					y++;
				}
			} else {
				// Down left
				int x = x1;
				int y = y1;
				int tilt = (x1 - x2) * 2;
				int maxError = y2 - y1;
				int error = 0;
				while (y <= y2) {
					IMAGE_TYPE::writePixel(target, x, y, color);
					error += tilt;
					if (error >= maxError) {
						x--;
						error -= maxError * 2;
					}
					y++;
				}
			}
		} else {
			if (x2 < x1) {
				swap(x1, x2);
				swap(y1, y2);
			}
			assert(x2 > x1);
			if (y2 > y1) {
				// Down right
				int x = x1;
				int y = y1;
				int tilt = (y2 - y1) * 2;
				int maxError = x2 - x1;
				int error = 0;
				while (x <= x2) {
					IMAGE_TYPE::writePixel(target, x, y, color);
					error += tilt;
					if (error >= maxError) {
						y++;
						error -= maxError * 2;
					}
					x++;
				}
			} else {
				// Up right
				int x = x1;
				int y = y1;
				int tilt = (y1 - y2) * 2;
				int maxError = x2 - x1;
				int error = 0;
				while (x <= x2) {
					IMAGE_TYPE::writePixel(target, x, y, color);
					error += tilt;
					if (error >= maxError) {
						y--;
						error -= maxError * 2;
					}
					x++;
				}
			}
		}
	}
}

void dsr::imageImpl_draw_line(ImageU8Impl& image, int32_t x1, int32_t y1, int32_t x2, int32_t y2, int color) {
	if (color < 0) { color = 0; }
	if (color > 255) { color = 255; }
	drawLineSuper<ImageU8Impl, uint8_t>(image, x1, y1, x2, y2, color);
}

void dsr::imageImpl_draw_line(ImageU16Impl& image, int32_t x1, int32_t y1, int32_t x2, int32_t y2, int color) {
	if (color < 0) { color = 0; }
	if (color > 65535) { color = 65535; }
	drawLineSuper<ImageU16Impl, uint16_t>(image, x1, y1, x2, y2, color);
}

void dsr::imageImpl_draw_line(ImageF32Impl& image, int32_t x1, int32_t y1, int32_t x2, int32_t y2, float color) {
	drawLineSuper<ImageF32Impl, float>(image, x1, y1, x2, y2, color);
}

void dsr::imageImpl_draw_line(ImageRgbaU8Impl& image, int32_t x1, int32_t y1, int32_t x2, int32_t y2, const ColorRgbaI32& color) {
	drawLineSuper<ImageRgbaU8Impl, Color4xU8>(image, x1, y1, x2, y2, image.packRgba(color.saturate()));
}

// -------------------------------- Drawing images --------------------------------

// A packet with the dimensions of an image
struct ImageDimensions {
	// width is the number of used pixels on each row.
	// height is the number of rows.
	// stride is the byte offset from one row to another including any padding.
	// pixelSize is the byte offset from one pixel to another from left to right.
	int32_t width, height, stride, pixelSize;
	ImageDimensions() : width(0), height(0), stride(0), pixelSize(0) {}
	ImageDimensions(const ImageImpl& image) :
	  width(image.width), height(image.height), stride(image.stride), pixelSize(image.pixelSize) {}
};

struct ImageWriter : public ImageDimensions {
	uint8_t *data;
	ImageWriter(const ImageDimensions &dimensions, uint8_t *data) :
	  ImageDimensions(dimensions), data(data) {}
};

struct ImageReader : public ImageDimensions {
	const uint8_t *data;
	ImageReader(const ImageDimensions &dimensions, const uint8_t *data) :
	  ImageDimensions(dimensions), data(data) {}
};

static ImageWriter getWriter(ImageImpl &image) {
	return ImageWriter(ImageDimensions(image), buffer_dangerous_getUnsafeData(image.buffer) + image.startOffset);
}

static ImageReader getReader(const ImageImpl &image) {
	return ImageReader(ImageDimensions(image), buffer_dangerous_getUnsafeData(image.buffer) + image.startOffset);
}

static ImageImpl getGenericSubImage(const ImageImpl &image, int32_t left, int32_t top, int32_t width, int32_t height) {
	assert(left >= 0 && top >= 0 && width >= 1 && height >= 1 && left + width <= image.width && top + height <= image.height);
	intptr_t newOffset = image.startOffset + (left * image.pixelSize) + (top * image.stride);
	return ImageImpl(width, height, image.stride, image.pixelSize, image.buffer, newOffset);
}

struct ImageIntersection {
	ImageWriter subTarget;
	ImageReader subSource;
	ImageIntersection(const ImageWriter &subTarget, const ImageReader &subSource) :
	  subTarget(subTarget), subSource(subSource) {}
	static bool canCreate(ImageImpl &target, const ImageImpl &source, int32_t left, int32_t top) {
		int32_t targetRegionRight = left + source.width;
		int32_t targetRegionBottom = top + source.height;
		return left < target.width && top < target.height && targetRegionRight > 0 && targetRegionBottom > 0;
	}
	// Only call if canCreate passed with the same arguments
	static ImageIntersection create(ImageImpl &target, const ImageImpl &source, int32_t left, int32_t top) {
		int32_t targetRegionRight = left + source.width;
		int32_t targetRegionBottom = top + source.height;
		assert(ImageIntersection::canCreate(target, source, left, top));
		// Check if the source has to be clipped
		if (left < 0 || top < 0 || targetRegionRight > target.width || targetRegionBottom > target.height) {
			int32_t clipLeft = std::max(0, -left);
			int32_t clipTop = std::max(0, -top);
			int32_t clipRight = std::max(0, targetRegionRight - target.width);
			int32_t clipBottom = std::max(0, targetRegionBottom - target.height);
			int32_t newWidth = source.width - (clipLeft + clipRight);
			int32_t newHeight = source.height - (clipTop + clipBottom);
			assert(newWidth > 0 && newHeight > 0);
			// Partial drawing
			ImageImpl subTarget = getGenericSubImage(target, left + clipLeft, top + clipTop, newWidth, newHeight);
			ImageImpl subSource = getGenericSubImage(source, clipLeft, clipTop, newWidth, newHeight);
			return ImageIntersection(getWriter(subTarget), getReader(subSource));
		} else {
			// Full drawing
			ImageImpl subTarget = getGenericSubImage(target, left, top, source.width, source.height);
			return ImageIntersection(getWriter(subTarget), getReader(source));
		}
	}
};

#define ITERATE_ROWS(WRITER, READER, OPERATION) \
{ \
	uint8_t *targetRow = WRITER.data; \
	const uint8_t *sourceRow = READER.data; \
	for (int32_t y = 0; y < READER.height; y++) { \
		OPERATION; \
		targetRow += WRITER.stride; \
		sourceRow += READER.stride; \
	} \
}

#define ITERATE_PIXELS(WRITER, READER, OPERATION) \
{ \
	uint8_t *targetRow = WRITER.data; \
	const uint8_t *sourceRow = READER.data; \
	for (int32_t y = 0; y < READER.height; y++) { \
		uint8_t *targetPixel = targetRow; \
		const uint8_t *sourcePixel = sourceRow; \
		for (int32_t x = 0; x < READER.width; x++) { \
			{OPERATION;} \
			targetPixel += WRITER.pixelSize; \
			sourcePixel += READER.pixelSize; \
		} \
		targetRow += WRITER.stride; \
		sourceRow += READER.stride; \
	} \
}

#define ITERATE_PIXELS_2(WRITER1, READER1, WRITER2, READER2, OPERATION) \
{ \
	uint8_t *targetRow1 = WRITER1.data; \
	uint8_t *targetRow2 = WRITER2.data; \
	const uint8_t *sourceRow1 = READER1.data; \
	const uint8_t *sourceRow2 = READER2.data; \
	int minWidth = std::min(READER1.width, READER2.width); \
	int minHeight = std::min(READER1.height, READER2.height); \
	for (int32_t y = 0; y < minHeight; y++) { \
		uint8_t *targetPixel1 = targetRow1; \
		uint8_t *targetPixel2 = targetRow2; \
		const uint8_t *sourcePixel1 = sourceRow1; \
		const uint8_t *sourcePixel2 = sourceRow2; \
		for (int32_t x = 0; x < minWidth; x++) { \
			{OPERATION;} \
			targetPixel1 += WRITER1.pixelSize; \
			targetPixel2 += WRITER2.pixelSize; \
			sourcePixel1 += READER1.pixelSize; \
			sourcePixel2 += READER2.pixelSize; \
		} \
		targetRow1 += WRITER1.stride; \
		targetRow2 += WRITER2.stride; \
		sourceRow1 += READER1.stride; \
		sourceRow2 += READER2.stride; \
	} \
}

#define ITERATE_PIXELS_3(WRITER1, READER1, WRITER2, READER2, WRITER3, READER3, OPERATION) \
{ \
	uint8_t *targetRow1 = WRITER1.data; \
	uint8_t *targetRow2 = WRITER2.data; \
	uint8_t *targetRow3 = WRITER3.data; \
	const uint8_t *sourceRow1 = READER1.data; \
	const uint8_t *sourceRow2 = READER2.data; \
	const uint8_t *sourceRow3 = READER3.data; \
	int minWidth = std::min(std::min(READER1.width, READER2.width), READER3.width); \
	int minHeight = std::min(std::min(READER1.height, READER2.height), READER3.height); \
	for (int32_t y = 0; y < minHeight; y++) { \
		uint8_t *targetPixel1 = targetRow1; \
		uint8_t *targetPixel2 = targetRow2; \
		uint8_t *targetPixel3 = targetRow3; \
		const uint8_t *sourcePixel1 = sourceRow1; \
		const uint8_t *sourcePixel2 = sourceRow2; \
		const uint8_t *sourcePixel3 = sourceRow3; \
		for (int32_t x = 0; x < minWidth; x++) { \
			{OPERATION;} \
			targetPixel1 += WRITER1.pixelSize; \
			targetPixel2 += WRITER2.pixelSize; \
			targetPixel3 += WRITER3.pixelSize; \
			sourcePixel1 += READER1.pixelSize; \
			sourcePixel2 += READER2.pixelSize; \
			sourcePixel3 += READER3.pixelSize; \
		} \
		targetRow1 += WRITER1.stride; \
		targetRow2 += WRITER2.stride; \
		targetRow3 += WRITER3.stride; \
		sourceRow1 += READER1.stride; \
		sourceRow2 += READER2.stride; \
		sourceRow3 += READER3.stride; \
	} \
}

static inline int saturateFloat(float value) {
	if (!(value >= 0.0f)) {
		// NaN or negative
		return 0;
	} else if (value > 255.0f) {
		// Too large
		return 255;
	} else {
		// Round to closest
		return (int)(value + 0.5f);
	}
}

// Copy data from one image region to another of the same size.
//   Packing order is reinterpreted without conversion.
static void copyImageData(ImageWriter writer, ImageReader reader) {
	assert(writer.width == reader.width && writer.height == reader.height && writer.pixelSize == reader.pixelSize);
	ITERATE_ROWS(writer, reader, std::memcpy(targetRow, sourceRow, reader.width * reader.pixelSize));
}

void dsr::imageImpl_drawCopy(ImageRgbaU8Impl& target, const ImageRgbaU8Impl& source, int32_t left, int32_t top) {
	if (ImageIntersection::canCreate(target, source, left, top)) {
		ImageIntersection intersection = ImageIntersection::create(target, source, left, top);
		if (target.packOrder == source.packOrder) {
			// No conversion needed
			copyImageData(intersection.subTarget, intersection.subSource);
		} else {
			// Read and repack to convert between different color formats
			ITERATE_PIXELS(intersection.subTarget, intersection.subSource,
				targetPixel[target.packOrder.redIndex]   = sourcePixel[source.packOrder.redIndex];
				targetPixel[target.packOrder.greenIndex] = sourcePixel[source.packOrder.greenIndex];
				targetPixel[target.packOrder.blueIndex]  = sourcePixel[source.packOrder.blueIndex];
				targetPixel[target.packOrder.alphaIndex] = sourcePixel[source.packOrder.alphaIndex];
			);
		}
	}
}
void dsr::imageImpl_drawCopy(ImageU8Impl& target, const ImageU8Impl& source, int32_t left, int32_t top) {
	if (ImageIntersection::canCreate(target, source, left, top)) {
		ImageIntersection intersection = ImageIntersection::create(target, source, left, top);
		copyImageData(intersection.subTarget, intersection.subSource);
	}
}
void dsr::imageImpl_drawCopy(ImageU16Impl& target, const ImageU16Impl& source, int32_t left, int32_t top) {
	if (ImageIntersection::canCreate(target, source, left, top)) {
		ImageIntersection intersection = ImageIntersection::create(target, source, left, top);
		copyImageData(intersection.subTarget, intersection.subSource);
	}
}
void dsr::imageImpl_drawCopy(ImageF32Impl& target, const ImageF32Impl& source, int32_t left, int32_t top) {
	if (ImageIntersection::canCreate(target, source, left, top)) {
		ImageIntersection intersection = ImageIntersection::create(target, source, left, top);
		copyImageData(intersection.subTarget, intersection.subSource);
	}
}
void dsr::imageImpl_drawCopy(ImageRgbaU8Impl& target, const ImageU8Impl& source, int32_t left, int32_t top) {
	if (ImageIntersection::canCreate(target, source, left, top)) {
		ImageIntersection intersection = ImageIntersection::create(target, source, left, top);
		ITERATE_PIXELS(intersection.subTarget, intersection.subSource,
			uint8_t luma = *sourcePixel;
			targetPixel[target.packOrder.redIndex]   = luma;
			targetPixel[target.packOrder.greenIndex] = luma;
			targetPixel[target.packOrder.blueIndex]  = luma;
			targetPixel[target.packOrder.alphaIndex] = 255;
		);
	}
}
void dsr::imageImpl_drawCopy(ImageRgbaU8Impl& target, const ImageU16Impl& source, int32_t left, int32_t top) {
	if (ImageIntersection::canCreate(target, source, left, top)) {
		ImageIntersection intersection = ImageIntersection::create(target, source, left, top);
		ITERATE_PIXELS(intersection.subTarget, intersection.subSource,
			int luma = *((const uint16_t*)sourcePixel);
			if (luma > 255) { luma = 255; }
			targetPixel[target.packOrder.redIndex]   = luma;
			targetPixel[target.packOrder.greenIndex] = luma;
			targetPixel[target.packOrder.blueIndex]  = luma;
			targetPixel[target.packOrder.alphaIndex] = 255;
		);
	}
}
void dsr::imageImpl_drawCopy(ImageRgbaU8Impl& target, const ImageF32Impl& source, int32_t left, int32_t top) {
	if (ImageIntersection::canCreate(target, source, left, top)) {
		ImageIntersection intersection = ImageIntersection::create(target, source, left, top);
		ITERATE_PIXELS(intersection.subTarget, intersection.subSource,
			int luma = saturateFloat(*((const float*)sourcePixel));
			targetPixel[target.packOrder.redIndex]   = luma;
			targetPixel[target.packOrder.greenIndex] = luma;
			targetPixel[target.packOrder.blueIndex]  = luma;
			targetPixel[target.packOrder.alphaIndex] = 255;
		);
	}
}
void dsr::imageImpl_drawCopy(ImageU8Impl& target, const ImageF32Impl& source, int32_t left, int32_t top) {
	if (ImageIntersection::canCreate(target, source, left, top)) {
		ImageIntersection intersection = ImageIntersection::create(target, source, left, top);
		ITERATE_PIXELS(intersection.subTarget, intersection.subSource,
			*targetPixel = saturateFloat(*((const float*)sourcePixel));
		);
	}
}
void dsr::imageImpl_drawCopy(ImageU8Impl& target, const ImageU16Impl& source, int32_t left, int32_t top) {
	if (ImageIntersection::canCreate(target, source, left, top)) {
		ImageIntersection intersection = ImageIntersection::create(target, source, left, top);
		ITERATE_PIXELS(intersection.subTarget, intersection.subSource,
			int luma = *((const uint16_t*)sourcePixel);
			if (luma > 255) { luma = 255; }
			*targetPixel = luma;
		);
	}
}
void dsr::imageImpl_drawCopy(ImageU16Impl& target, const ImageU8Impl& source, int32_t left, int32_t top) {
	if (ImageIntersection::canCreate(target, source, left, top)) {
		ImageIntersection intersection = ImageIntersection::create(target, source, left, top);
		ITERATE_PIXELS(intersection.subTarget, intersection.subSource,
			*((uint16_t*)targetPixel) = *sourcePixel;
		);
	}
}
void dsr::imageImpl_drawCopy(ImageU16Impl& target, const ImageF32Impl& source, int32_t left, int32_t top) {
	if (ImageIntersection::canCreate(target, source, left, top)) {
		ImageIntersection intersection = ImageIntersection::create(target, source, left, top);
		ITERATE_PIXELS(intersection.subTarget, intersection.subSource,
			int luma = *((const float*)sourcePixel);
			if (luma < 0) { luma = 0; }
			if (luma > 65535) { luma = 65535; }
			*((uint16_t*)targetPixel) = *sourcePixel;
		);
	}
}
void dsr::imageImpl_drawCopy(ImageF32Impl& target, const ImageU8Impl& source, int32_t left, int32_t top) {
	if (ImageIntersection::canCreate(target, source, left, top)) {
		ImageIntersection intersection = ImageIntersection::create(target, source, left, top);
		ITERATE_PIXELS(intersection.subTarget, intersection.subSource,
			*((float*)targetPixel) = (float)(*sourcePixel);
		);
	}
}
void dsr::imageImpl_drawCopy(ImageF32Impl& target, const ImageU16Impl& source, int32_t left, int32_t top) {
	if (ImageIntersection::canCreate(target, source, left, top)) {
		ImageIntersection intersection = ImageIntersection::create(target, source, left, top);
		ITERATE_PIXELS(intersection.subTarget, intersection.subSource,
			int luma = *((const uint16_t*)sourcePixel);
			if (luma > 255) { luma = 255; }
			*((float*)targetPixel) = (float)luma;
		);
	}
}

void dsr::imageImpl_drawAlphaFilter(ImageRgbaU8Impl& target, const ImageRgbaU8Impl& source, int32_t left, int32_t top) {
	if (ImageIntersection::canCreate(target, source, left, top)) {
		ImageIntersection intersection = ImageIntersection::create(target, source, left, top);
		// Read and repack to convert between different color formats
		ITERATE_PIXELS(intersection.subTarget, intersection.subSource,
			// Optimized for anti-aliasing, where most alpha values are 0 or 255
			uint32_t sourceRatio = sourcePixel[source.packOrder.alphaIndex];
			if (sourceRatio > 0) {
				if (sourceRatio == 255) {
					targetPixel[target.packOrder.redIndex]   = sourcePixel[source.packOrder.redIndex];
					targetPixel[target.packOrder.greenIndex] = sourcePixel[source.packOrder.greenIndex];
					targetPixel[target.packOrder.blueIndex]  = sourcePixel[source.packOrder.blueIndex];
					targetPixel[target.packOrder.alphaIndex] = 255;
				} else {
					uint32_t targetRatio = 255 - sourceRatio;
					targetPixel[target.packOrder.redIndex]   = mulByte_8(targetPixel[target.packOrder.redIndex], targetRatio) + mulByte_8(sourcePixel[source.packOrder.redIndex], sourceRatio);
					targetPixel[target.packOrder.greenIndex] = mulByte_8(targetPixel[target.packOrder.greenIndex], targetRatio) + mulByte_8(sourcePixel[source.packOrder.greenIndex], sourceRatio);
					targetPixel[target.packOrder.blueIndex]  = mulByte_8(targetPixel[target.packOrder.blueIndex], targetRatio) + mulByte_8(sourcePixel[source.packOrder.blueIndex], sourceRatio);
					targetPixel[target.packOrder.alphaIndex] = mulByte_8(targetPixel[target.packOrder.alphaIndex], targetRatio) + sourceRatio;
				}
			}
		);
	}
}

void dsr::imageImpl_drawMaxAlpha(ImageRgbaU8Impl& target, const ImageRgbaU8Impl& source, int32_t left, int32_t top, int32_t sourceAlphaOffset) {
	if (ImageIntersection::canCreate(target, source, left, top)) {
		ImageIntersection intersection = ImageIntersection::create(target, source, left, top);
		// Read and repack to convert between different color formats
		if (sourceAlphaOffset == 0) {
			ITERATE_PIXELS(intersection.subTarget, intersection.subSource,
				int sourceAlpha = sourcePixel[source.packOrder.alphaIndex];
				if (sourceAlpha > targetPixel[target.packOrder.alphaIndex]) {
					targetPixel[target.packOrder.redIndex]   = sourcePixel[source.packOrder.redIndex];
					targetPixel[target.packOrder.greenIndex] = sourcePixel[source.packOrder.greenIndex];
					targetPixel[target.packOrder.blueIndex]  = sourcePixel[source.packOrder.blueIndex];
					targetPixel[target.packOrder.alphaIndex] = sourceAlpha;
				}
			);
		} else {
			ITERATE_PIXELS(intersection.subTarget, intersection.subSource,
				int sourceAlpha = sourcePixel[source.packOrder.alphaIndex];
				if (sourceAlpha > 0) {
					sourceAlpha += sourceAlphaOffset;
					if (sourceAlpha > targetPixel[target.packOrder.alphaIndex]) {
						targetPixel[target.packOrder.redIndex]   = sourcePixel[source.packOrder.redIndex];
						targetPixel[target.packOrder.greenIndex] = sourcePixel[source.packOrder.greenIndex];
						targetPixel[target.packOrder.blueIndex]  = sourcePixel[source.packOrder.blueIndex];
						if (sourceAlpha < 0) { sourceAlpha = 0; }
						if (sourceAlpha > 255) { sourceAlpha = 255; }
						targetPixel[target.packOrder.alphaIndex] = sourceAlpha;
					}
				}
			);
		}
	}
}

void dsr::imageImpl_drawAlphaClip(ImageRgbaU8Impl& target, const ImageRgbaU8Impl& source, int32_t left, int32_t top, int32_t threshold) {
	if (ImageIntersection::canCreate(target, source, left, top)) {
		ImageIntersection intersection = ImageIntersection::create(target, source, left, top);
		// Read and repack to convert between different color formats
		ITERATE_PIXELS(intersection.subTarget, intersection.subSource,
			if (sourcePixel[source.packOrder.alphaIndex] > threshold) {
				targetPixel[target.packOrder.redIndex]   = sourcePixel[source.packOrder.redIndex];
				targetPixel[target.packOrder.greenIndex] = sourcePixel[source.packOrder.greenIndex];
				targetPixel[target.packOrder.blueIndex]  = sourcePixel[source.packOrder.blueIndex];
				targetPixel[target.packOrder.alphaIndex] = 255;
			}
		);
	}
}

template <bool FULL_ALPHA>
static void drawSilhouette_template(ImageRgbaU8Impl& target, const ImageU8Impl& source, const ColorRgbaI32& color, int32_t left, int32_t top) {
	if (ImageIntersection::canCreate(target, source, left, top)) {
		ImageIntersection intersection = ImageIntersection::create(target, source, left, top);
		// Read and repack to convert between different color formats
		ITERATE_PIXELS(intersection.subTarget, intersection.subSource,
			uint32_t sourceRatio;
			if (FULL_ALPHA) {
				sourceRatio = *sourcePixel;
			} else {
				sourceRatio = mulByte_8(*sourcePixel, color.alpha);
			}
			if (sourceRatio > 0) {
				if (sourceRatio == 255) {
					targetPixel[target.packOrder.redIndex]   = color.red;
					targetPixel[target.packOrder.greenIndex] = color.green;
					targetPixel[target.packOrder.blueIndex]  = color.blue;
					targetPixel[target.packOrder.alphaIndex] = 255;
				} else {
					uint32_t targetRatio = 255 - sourceRatio;
					targetPixel[target.packOrder.redIndex]   = mulByte_8(targetPixel[target.packOrder.redIndex], targetRatio) + mulByte_8(color.red, sourceRatio);
					targetPixel[target.packOrder.greenIndex] = mulByte_8(targetPixel[target.packOrder.greenIndex], targetRatio) + mulByte_8(color.green, sourceRatio);
					targetPixel[target.packOrder.blueIndex]  = mulByte_8(targetPixel[target.packOrder.blueIndex], targetRatio) + mulByte_8(color.blue, sourceRatio);
					targetPixel[target.packOrder.alphaIndex] = mulByte_8(targetPixel[target.packOrder.alphaIndex], targetRatio) + sourceRatio;
				}
			}
		);
	}
}
void dsr::imageImpl_drawSilhouette(ImageRgbaU8Impl& target, const ImageU8Impl& source, const ColorRgbaI32& color, int32_t left, int32_t top) {
	if (color.alpha > 0) {
		ColorRgbaI32 saturatedColor = color.saturate();
		if (color.alpha < 255) {
			drawSilhouette_template<false>(target, source, saturatedColor, left, top);
		} else {
			drawSilhouette_template<true>(target, source, saturatedColor, left, top);
		}
	}
}

void dsr::imageImpl_drawHigher(ImageU16Impl& targetHeight, const ImageU16Impl& sourceHeight, int32_t left, int32_t top, int32_t sourceHeightOffset) {
	if (ImageIntersection::canCreate(targetHeight, sourceHeight, left, top)) {
		ImageIntersection intersectionH = ImageIntersection::create(targetHeight, sourceHeight, left, top);
		ITERATE_PIXELS(intersectionH.subTarget, intersectionH.subSource,
			int32_t newHeight = *((const uint16_t*)sourcePixel);
			if (newHeight > 0) {
				newHeight += sourceHeightOffset;
				if (newHeight < 0) { newHeight = 0; }
				if (newHeight > 65535) { newHeight = 65535; }
				if (newHeight > 0 && newHeight > *((uint16_t*)targetPixel)) {
					*((uint16_t*)targetPixel) = newHeight;
				}
			}
		);
	}
}
void dsr::imageImpl_drawHigher(ImageU16Impl& targetHeight, const ImageU16Impl& sourceHeight, ImageRgbaU8Impl& targetA, const ImageRgbaU8Impl& sourceA,
  int32_t left, int32_t top, int32_t sourceHeightOffset) {
	assert(sourceA.width == sourceHeight.width);
	assert(sourceA.height == sourceHeight.height);
	if (ImageIntersection::canCreate(targetHeight, sourceHeight, left, top)) {
		ImageIntersection intersectionH = ImageIntersection::create(targetHeight, sourceHeight, left, top);
		ImageIntersection intersectionA = ImageIntersection::create(targetA, sourceA, left, top);
		ITERATE_PIXELS_2(intersectionH.subTarget, intersectionH.subSource, intersectionA.subTarget, intersectionA.subSource,
			int32_t newHeight = *((const uint16_t*)sourcePixel1);
			if (newHeight > 0) {
				newHeight += sourceHeightOffset;
				if (newHeight < 0) { newHeight = 0; }
				if (newHeight > 65535) { newHeight = 65535; }
				if (newHeight > *((uint16_t*)targetPixel1)) {
					*((uint16_t*)targetPixel1) = newHeight;
					targetPixel2[targetA.packOrder.redIndex]   = sourcePixel2[sourceA.packOrder.redIndex];
					targetPixel2[targetA.packOrder.greenIndex] = sourcePixel2[sourceA.packOrder.greenIndex];
					targetPixel2[targetA.packOrder.blueIndex]  = sourcePixel2[sourceA.packOrder.blueIndex];
					targetPixel2[targetA.packOrder.alphaIndex] = sourcePixel2[sourceA.packOrder.alphaIndex];
				}
			}
		);
	}
}
void dsr::imageImpl_drawHigher(ImageU16Impl& targetHeight, const ImageU16Impl& sourceHeight, ImageRgbaU8Impl& targetA, const ImageRgbaU8Impl& sourceA,
  ImageRgbaU8Impl& targetB, const ImageRgbaU8Impl& sourceB, int32_t left, int32_t top, int32_t sourceHeightOffset) {
	assert(sourceA.width == sourceHeight.width);
	assert(sourceA.height == sourceHeight.height);
	assert(sourceB.width == sourceHeight.width);
	assert(sourceB.height == sourceHeight.height);
	if (ImageIntersection::canCreate(targetHeight, sourceHeight, left, top)) {
		ImageIntersection intersectionH = ImageIntersection::create(targetHeight, sourceHeight, left, top);
		ImageIntersection intersectionA = ImageIntersection::create(targetA, sourceA, left, top);
		ImageIntersection intersectionB = ImageIntersection::create(targetB, sourceB, left, top);
		ITERATE_PIXELS_3(intersectionH.subTarget, intersectionH.subSource, intersectionA.subTarget, intersectionA.subSource, intersectionB.subTarget, intersectionB.subSource,
			int32_t newHeight = *((const uint16_t*)sourcePixel1);
			if (newHeight > 0) {
				newHeight += sourceHeightOffset;
				if (newHeight < 0) { newHeight = 0; }
				if (newHeight > 65535) { newHeight = 65535; }
				if (newHeight > *((uint16_t*)targetPixel1)) {
					*((uint16_t*)targetPixel1) = newHeight;
					targetPixel2[targetA.packOrder.redIndex]   = sourcePixel2[sourceA.packOrder.redIndex];
					targetPixel2[targetA.packOrder.greenIndex] = sourcePixel2[sourceA.packOrder.greenIndex];
					targetPixel2[targetA.packOrder.blueIndex]  = sourcePixel2[sourceA.packOrder.blueIndex];
					targetPixel2[targetA.packOrder.alphaIndex] = sourcePixel2[sourceA.packOrder.alphaIndex];
					targetPixel3[targetB.packOrder.redIndex]   = sourcePixel3[sourceB.packOrder.redIndex];
					targetPixel3[targetB.packOrder.greenIndex] = sourcePixel3[sourceB.packOrder.greenIndex];
					targetPixel3[targetB.packOrder.blueIndex]  = sourcePixel3[sourceB.packOrder.blueIndex];
					targetPixel3[targetB.packOrder.alphaIndex] = sourcePixel3[sourceB.packOrder.alphaIndex];
				}
			}
		);
	}
}

void dsr::imageImpl_drawHigher(ImageF32Impl& targetHeight, const ImageF32Impl& sourceHeight, int32_t left, int32_t top, float sourceHeightOffset) {
	if (ImageIntersection::canCreate(targetHeight, sourceHeight, left, top)) {
		ImageIntersection intersectionH = ImageIntersection::create(targetHeight, sourceHeight, left, top);
		ITERATE_PIXELS(intersectionH.subTarget, intersectionH.subSource,
			float newHeight = *((const float*)sourcePixel);
			if (newHeight > -std::numeric_limits<float>::infinity()) {
				newHeight += sourceHeightOffset;
				if (newHeight > *((float*)targetPixel)) {
					*((float*)targetPixel) = newHeight;
				}
			}
		);
	}
}
void dsr::imageImpl_drawHigher(ImageF32Impl& targetHeight, const ImageF32Impl& sourceHeight, ImageRgbaU8Impl& targetA, const ImageRgbaU8Impl& sourceA,
  int32_t left, int32_t top, float sourceHeightOffset) {
	assert(sourceA.width == sourceHeight.width);
	assert(sourceA.height == sourceHeight.height);
	if (ImageIntersection::canCreate(targetHeight, sourceHeight, left, top)) {
		ImageIntersection intersectionH = ImageIntersection::create(targetHeight, sourceHeight, left, top);
		ImageIntersection intersectionA = ImageIntersection::create(targetA, sourceA, left, top);
		ITERATE_PIXELS_2(intersectionH.subTarget, intersectionH.subSource, intersectionA.subTarget, intersectionA.subSource,
			float newHeight = *((const float*)sourcePixel1);
			if (newHeight > -std::numeric_limits<float>::infinity()) {
				newHeight += sourceHeightOffset;
				if (newHeight > *((float*)targetPixel1)) {
					*((float*)targetPixel1) = newHeight;
					targetPixel2[targetA.packOrder.redIndex]   = sourcePixel2[sourceA.packOrder.redIndex];
					targetPixel2[targetA.packOrder.greenIndex] = sourcePixel2[sourceA.packOrder.greenIndex];
					targetPixel2[targetA.packOrder.blueIndex]  = sourcePixel2[sourceA.packOrder.blueIndex];
					targetPixel2[targetA.packOrder.alphaIndex] = sourcePixel2[sourceA.packOrder.alphaIndex];
				}
			}
		);
	}
}
void dsr::imageImpl_drawHigher(ImageF32Impl& targetHeight, const ImageF32Impl& sourceHeight, ImageRgbaU8Impl& targetA, const ImageRgbaU8Impl& sourceA,
  ImageRgbaU8Impl& targetB, const ImageRgbaU8Impl& sourceB, int32_t left, int32_t top, float sourceHeightOffset) {
	assert(sourceA.width == sourceHeight.width);
	assert(sourceA.height == sourceHeight.height);
	assert(sourceB.width == sourceHeight.width);
	assert(sourceB.height == sourceHeight.height);
	if (ImageIntersection::canCreate(targetHeight, sourceHeight, left, top)) {
		ImageIntersection intersectionH = ImageIntersection::create(targetHeight, sourceHeight, left, top);
		ImageIntersection intersectionA = ImageIntersection::create(targetA, sourceA, left, top);
		ImageIntersection intersectionB = ImageIntersection::create(targetB, sourceB, left, top);
		ITERATE_PIXELS_3(intersectionH.subTarget, intersectionH.subSource, intersectionA.subTarget, intersectionA.subSource, intersectionB.subTarget, intersectionB.subSource,
			float newHeight = *((const float*)sourcePixel1);
			if (newHeight > -std::numeric_limits<float>::infinity()) {
				newHeight += sourceHeightOffset;
				if (newHeight > *((float*)targetPixel1)) {
					*((float*)targetPixel1) = newHeight;
					targetPixel2[targetA.packOrder.redIndex]   = sourcePixel2[sourceA.packOrder.redIndex];
					targetPixel2[targetA.packOrder.greenIndex] = sourcePixel2[sourceA.packOrder.greenIndex];
					targetPixel2[targetA.packOrder.blueIndex]  = sourcePixel2[sourceA.packOrder.blueIndex];
					targetPixel2[targetA.packOrder.alphaIndex] = sourcePixel2[sourceA.packOrder.alphaIndex];
					targetPixel3[targetB.packOrder.redIndex]   = sourcePixel3[sourceB.packOrder.redIndex];
					targetPixel3[targetB.packOrder.greenIndex] = sourcePixel3[sourceB.packOrder.greenIndex];
					targetPixel3[targetB.packOrder.blueIndex]  = sourcePixel3[sourceB.packOrder.blueIndex];
					targetPixel3[targetB.packOrder.alphaIndex] = sourcePixel3[sourceB.packOrder.alphaIndex];
				}
			}
		);
	}
}


// -------------------------------- Resize --------------------------------


static inline U32x4 ColorRgbaI32_to_U32x4(const ColorRgbaI32& color) {
	return U32x4(color.red, color.green, color.blue, color.alpha);
}

static inline ColorRgbaI32 U32x4_to_ColorRgbaI32(const U32x4& color) {
	UVector4D vResult = color.get();
	return ColorRgbaI32(vResult.x, vResult.y, vResult.z, vResult.w);
}

// Uniform linear interpolation of colors from a 16-bit sub-pixel weight
// Pre-condition0 <= fineRatio <= 65536
// Post-condition: Returns colorA * (1 - (fineRatio / 65536)) + colorB * (fineRatio / 65536)
static inline U32x4 mixColorsUniform(const U32x4 &colorA, const U32x4 &colorB, uint32_t fineRatio) {
	uint16_t ratio = fineRatio >> 8;
	uint16_t invRatio = 256 - ratio;
	U16x8 weightA = U16x8(invRatio);
	U16x8 weightB = U16x8(ratio);
	U32x4 lowMask(0x00FF00FFu);
	U16x8 lowColorA = U16x8(colorA & lowMask);
	U16x8 lowColorB = U16x8(colorB & lowMask);
	U32x4 highMask(0xFF00FF00u);
	U16x8 highColorA = U16x8((colorA & highMask) >> 8);
	U16x8 highColorB = U16x8((colorB & highMask) >> 8);
	U32x4 lowColor = (((lowColorA * weightA) + (lowColorB * weightB))).get_U32();
	U32x4 highColor = (((highColorA * weightA) + (highColorB * weightB))).get_U32();
	return (((lowColor >> 8) & lowMask) | (highColor & highMask));
}

#define READ_RGBAU8_CLAMP(X,Y) ImageRgbaU8Impl::unpackRgba(ImageRgbaU8Impl::readPixel_clamp(source, X, Y), source.packOrder)
#define READ_RGBAU8_CLAMP_SIMD(X,Y) ColorRgbaI32_to_U32x4(READ_RGBAU8_CLAMP(X,Y))

// Fixed-precision decimal system with 16-bit indices and 16-bit sub-pixel weights
static const uint32_t interpolationFullPixel = 65536;
static const uint32_t interpolationHalfPixel = interpolationFullPixel / 2;
// Modulo mask for values greater than or equal to 0 and lesser than interpolationFullPixel
static const uint32_t interpolationWeightMask = interpolationFullPixel - 1;

template <bool BILINEAR>
static uint32_t samplePixel(const ImageRgbaU8Impl& target, const ImageRgbaU8Impl& source, uint32_t leftX, uint32_t upperY, uint32_t rightRatio, uint32_t lowerRatio) {
	if (BILINEAR) {
		uint32_t upperRatio = 65536 - lowerRatio;
		uint32_t leftRatio = 65536 - rightRatio;
		U32x4 vUpperLeftColor = READ_RGBAU8_CLAMP_SIMD(leftX, upperY);
		U32x4 vUpperRightColor = READ_RGBAU8_CLAMP_SIMD(leftX + 1, upperY);
		U32x4 vLowerLeftColor = READ_RGBAU8_CLAMP_SIMD(leftX, upperY + 1);
		U32x4 vLowerRightColor = READ_RGBAU8_CLAMP_SIMD(leftX + 1, upperY + 1);
		U32x4 vLeftRatio = U32x4(leftRatio);
		U32x4 vRightRatio = U32x4(rightRatio);
		U32x4 vUpperColor = ((vUpperLeftColor * vLeftRatio) + (vUpperRightColor * vRightRatio)) >> 16;
		U32x4 vLowerColor = ((vLowerLeftColor * vLeftRatio) + (vLowerRightColor * vRightRatio)) >> 16;
		U32x4 vCenterColor = ((vUpperColor * upperRatio) + (vLowerColor * lowerRatio)) >> 16;
		return (target.packRgba(U32x4_to_ColorRgbaI32(vCenterColor))).packed;
	} else {
		return (target.packRgba(READ_RGBAU8_CLAMP(leftX, upperY))).packed;
	}
}

template <bool BILINEAR>
static uint8_t samplePixel(const ImageU8Impl& target, const ImageU8Impl& source, uint32_t leftX, uint32_t upperY, uint32_t rightRatio, uint32_t lowerRatio) {
	if (BILINEAR) {
		uint32_t upperRatio = 65536 - lowerRatio;
		uint32_t leftRatio = 65536 - rightRatio;
		uint32_t upperLeftLuma = ImageU8Impl::readPixel_clamp(source, leftX, upperY);
		uint32_t upperRightLuma = ImageU8Impl::readPixel_clamp(source, leftX + 1, upperY);
		uint32_t lowerLeftLuma = ImageU8Impl::readPixel_clamp(source, leftX, upperY + 1);
		uint32_t lowerRightLuma = ImageU8Impl::readPixel_clamp(source, leftX + 1, upperY + 1);
		uint32_t upperLuma = ((upperLeftLuma * leftRatio) + (upperRightLuma * rightRatio)) >> 16;
		uint32_t lowerLuma = ((lowerLeftLuma * leftRatio) + (lowerRightLuma * rightRatio)) >> 16;
		return ((upperLuma * upperRatio) + (lowerLuma * lowerRatio)) >> 16;
	} else {
		return ImageU8Impl::readPixel_clamp(source, leftX, upperY);
	}
}

// BILINEAR: Enables linear interpolation
// scaleRegion:
//     The stretched location of the source image in the target image
//     Making it smaller than the target image will fill the outside with stretched pixels
//     Allowing the caller to crop away parts of the source image that aren't interesting
//     Can be used to round the region to a multiple of the input size for a fixed pixel size
template <bool BILINEAR, typename IMAGE_TYPE, typename PIXEL_TYPE>
static void resize_reference(IMAGE_TYPE& target, const IMAGE_TYPE& source, const IRect& scaleRegion) {
	// Reference implementation

	// Offset in source pixels per target pixel
	int32_t offsetX = interpolationFullPixel * source.width / scaleRegion.width();
	int32_t offsetY = interpolationFullPixel * source.height / scaleRegion.height();
	int32_t startX = interpolationFullPixel * scaleRegion.left() + offsetX / 2;
	int32_t startY = interpolationFullPixel * scaleRegion.top() + offsetY / 2;
	if (BILINEAR) {
		startX -= interpolationHalfPixel;
		startY -= interpolationHalfPixel;
	}
	SafePointer<PIXEL_TYPE> targetRow = imageInternal::getSafeData<PIXEL_TYPE>(target);
	int32_t readY = startY;
	for (int32_t y = 0; y < target.height; y++) {
		int32_t naturalY = readY;
		if (naturalY < 0) { naturalY = 0; }
		uint32_t sampleY = (uint32_t)naturalY;
		uint32_t upperY = sampleY >> 16;
		uint32_t lowerRatio = sampleY & interpolationWeightMask;
		SafePointer<PIXEL_TYPE> targetPixel = targetRow;
		int32_t readX = startX;
		for (int32_t x = 0; x < target.width; x++) {
			int32_t naturalX = readX;
			if (naturalX < 0) { naturalX = 0; }
			uint32_t sampleX = (uint32_t)naturalX;
			uint32_t leftX = sampleX >> 16;
			uint32_t rightRatio = sampleX & interpolationWeightMask;
			*targetPixel = samplePixel<BILINEAR>(target, source, leftX, upperY, rightRatio, lowerRatio);
			targetPixel += 1;
			readX += offsetX;
		}
		targetRow.increaseBytes(target.stride);
		readY += offsetY;
	}
}

// BILINEAR: Enables linear interpolation
// SIMD_ALIGNED: Each line starts 16-byte aligned, has a stride divisible with 16-bytes and is allowed to overwrite padding.
template <bool BILINEAR, bool SIMD_ALIGNED>
static void resize_optimized(ImageRgbaU8Impl& target, const ImageRgbaU8Impl& source, const IRect& scaleRegion) {
	// Get source information
	// Compare dimensions
	const bool sameWidth = source.width == scaleRegion.width() && scaleRegion.left() == 0;
	const bool sameHeight = source.height == scaleRegion.height() && scaleRegion.top() == 0;
	const bool samePackOrder = target.packOrder.packOrderIndex == source.packOrder.packOrderIndex;
	if (sameWidth && sameHeight) {
		// No need to resize, just make a copy to save time
		imageImpl_drawCopy(target, source);
	} else if (sameWidth && (samePackOrder || BILINEAR)) {
		// Only vertical interpolation

		// Offset in source pixels per target pixel
		int32_t offsetY = interpolationFullPixel * source.height / scaleRegion.height();
		int32_t startY = interpolationFullPixel * scaleRegion.top() + offsetY / 2;
		if (BILINEAR) {
			startY -= interpolationHalfPixel;
		}
		SafePointer<uint32_t> targetRow = imageInternal::getSafeData<uint32_t>(target);
		int32_t readY = startY;
		for (int32_t y = 0; y < target.height; y++) {
			int32_t naturalY = readY;
			if (naturalY < 0) { naturalY = 0; }
			uint32_t sampleY = (uint32_t)naturalY;
			uint32_t upperY = sampleY >> 16;
			uint32_t lowerY = upperY + 1;
			if (upperY >= (uint32_t)source.height) upperY = source.height - 1;
			if (lowerY >= (uint32_t)source.height) lowerY = source.height - 1;
			if (BILINEAR) {
				uint32_t lowerRatio = sampleY & interpolationWeightMask;
				uint32_t upperRatio = 65536 - lowerRatio;
				SafePointer<uint32_t> targetPixel = targetRow;
				if (SIMD_ALIGNED) {
					const SafePointer<uint32_t> sourceRowUpper = imageInternal::getSafeData<uint32_t>(source, upperY);
					const SafePointer<uint32_t> sourceRowLower = imageInternal::getSafeData<uint32_t>(source, lowerY);
					for (int32_t x = 0; x < target.width; x += 4) {
						ALIGN16 U32x4 vUpperPackedColor = U32x4::readAligned(sourceRowUpper, "resize_optimized @ read vUpperPackedColor");
						ALIGN16 U32x4 vLowerPackedColor = U32x4::readAligned(sourceRowLower, "resize_optimized @ read vLowerPackedColor");
						ALIGN16 U32x4 vCenterColor = mixColorsUniform(vUpperPackedColor, vLowerPackedColor, lowerRatio);
						vCenterColor.writeAligned(targetPixel, "resize_optimized @ write vCenterColor");
						sourceRowUpper += 4;
						sourceRowLower += 4;
						targetPixel += 4;
					}
				} else {
					for (int32_t x = 0; x < target.width; x++) {
						ALIGN16 U32x4 vUpperColor = READ_RGBAU8_CLAMP_SIMD(x, upperY);
						ALIGN16 U32x4 vLowerColor = READ_RGBAU8_CLAMP_SIMD(x, lowerY);
						ALIGN16 U32x4 vCenterColor = ((vUpperColor * upperRatio) + (vLowerColor * lowerRatio)) >> 16;
						ColorRgbaI32 finalColor = U32x4_to_ColorRgbaI32(vCenterColor);
						*targetPixel = target.packRgba(finalColor).packed;
						targetPixel += 1;
					}
				}
			} else {
				const SafePointer<uint32_t> sourceRowUpper = imageInternal::getSafeData<uint32_t>(source, upperY);
				// Nearest neighbor sampling from a same width can be done using one copy per row
				safeMemoryCopy(targetRow, sourceRowUpper, source.width * 4);
			}
			targetRow.increaseBytes(target.stride);
			readY += offsetY;
		}
	} else if (sameHeight) {
		// Only horizontal interpolation

		// Offset in source pixels per target pixel
		int32_t offsetX = interpolationFullPixel * source.width / scaleRegion.width();
		int32_t startX = interpolationFullPixel * scaleRegion.left() + offsetX / 2;
		if (BILINEAR) {
			startX -= interpolationHalfPixel;
		}
		SafePointer<uint32_t> targetRow = imageInternal::getSafeData<uint32_t>(target);
		for (int32_t y = 0; y < target.height; y++) {
			SafePointer<uint32_t> targetPixel = targetRow;
			int32_t readX = startX;
			for (int32_t x = 0; x < target.width; x++) {
				int32_t naturalX = readX;
				if (naturalX < 0) { naturalX = 0; }
				uint32_t sampleX = (uint32_t)naturalX;
				uint32_t leftX = sampleX >> 16;
				uint32_t rightX = leftX + 1;
				uint32_t rightRatio = sampleX & interpolationWeightMask;
				uint32_t leftRatio = 65536 - rightRatio;
				ColorRgbaI32 finalColor;
				if (BILINEAR) {
					ALIGN16 U32x4 vLeftColor = READ_RGBAU8_CLAMP_SIMD(leftX, y);
					ALIGN16 U32x4 vRightColor = READ_RGBAU8_CLAMP_SIMD(rightX, y);
					ALIGN16 U32x4 vCenterColor = ((vLeftColor * leftRatio) + (vRightColor * rightRatio)) >> 16;
					finalColor = U32x4_to_ColorRgbaI32(vCenterColor);
				} else {
					finalColor = READ_RGBAU8_CLAMP(leftX, y);
				}
				*targetPixel = target.packRgba(finalColor).packed;
				targetPixel += 1;
				readX += offsetX;
			}
			targetRow.increaseBytes(target.stride);
		}
	} else {
		// Call the reference implementation
		resize_reference<BILINEAR, ImageRgbaU8Impl, uint32_t>(target, source, scaleRegion);
	}
}

// Returns true iff each line start in image is aligned with 16 bytes
//   Often not the case for sub-images, even if the parent image is aligned
static bool imageIs16ByteAligned(const ImageImpl& image) {
	return (uint32_t)((image.stride & 15) == 0 && ((uintptr_t)(imageInternal::getSafeData<uint8_t>(image).getUnsafe()) & 15) == 0);
}

// Converting run-time flags into compile-time constants
static void resize_aux(ImageRgbaU8Impl& target, const ImageRgbaU8Impl& source, bool interpolate, bool paddWrite, const IRect& scaleRegion) {
	// If writing to padding is allowed and both images are 16-byte aligned with the same pack order
	if (paddWrite && imageIs16ByteAligned(source) && imageIs16ByteAligned(target)) {
		// SIMD resize allowed
		if (interpolate) {
			resize_optimized<true, true>(target, source, scaleRegion);
		} else {
			resize_optimized<false, true>(target, source, scaleRegion);
		}
	} else {
		// Non-SIMD resize
		if (interpolate) {
			resize_optimized<true, false>(target, source, scaleRegion);
		} else {
			resize_optimized<false, false>(target, source, scaleRegion);
		}
	}
}

// TODO: Optimize monochrome resizing.
static void resize_aux(ImageU8Impl& target, const ImageU8Impl& source, bool interpolate, bool paddWrite, const IRect& scaleRegion) {
	if (interpolate) {
		resize_reference<true, ImageU8Impl, uint8_t>(target, source, scaleRegion);
	} else {
		resize_reference<false, ImageU8Impl, uint8_t>(target, source, scaleRegion);
	}
}

// Creating an image to replacedImage with the same pack order as originalImage when applicable to the image format.
static ImageRgbaU8Impl createWithSamePackOrder(const ImageRgbaU8Impl& originalImage, int32_t width, int32_t height) {
	return ImageRgbaU8Impl(width, height, originalImage.packOrder.packOrderIndex);
}
static ImageU8Impl createWithSamePackOrder(const ImageU8Impl& originalImage, int32_t width, int32_t height) {
	return ImageU8Impl(width, height);
}

template <typename IMAGE_TYPE>
void resizeToTarget(IMAGE_TYPE& target, const IMAGE_TYPE& source, bool interpolate) {
	IRect scaleRegion = imageInternal::getBound(target);
	if (target.width != source.width && target.height > source.height) {
		// Upscaling is faster in two steps by both reusing the horizontal interpolation and vectorizing the vertical interpolation.
		int tempWidth = target.width;
		int tempHeight = source.height;
		IRect tempScaleRegion = IRect(scaleRegion.left(), 0, scaleRegion.width(), source.height);
		// Create a temporary buffer.
		IMAGE_TYPE newTempImage = createWithSamePackOrder(target, tempWidth, tempHeight);
		resize_aux(newTempImage, source, interpolate, true, tempScaleRegion);
		resize_aux(target, newTempImage, interpolate, true, scaleRegion);
	} else {
		// Downscaling or only changing one dimension is faster in one step.
		resize_aux(target, source, interpolate, true, scaleRegion);
	}
}

void dsr::imageImpl_resizeToTarget(ImageRgbaU8Impl& target, const ImageRgbaU8Impl& source, bool interpolate) {
	resizeToTarget<ImageRgbaU8Impl>(target, source, interpolate);
}

void dsr::imageImpl_resizeToTarget(ImageU8Impl& target, const ImageU8Impl& source, bool interpolate) {
	resizeToTarget<ImageU8Impl>(target, source, interpolate);
}

template <bool CONVERT_COLOR>
static inline Color4xU8 convertRead(const ImageRgbaU8Impl& target, const ImageRgbaU8Impl& source, int x, int y) {
	Color4xU8 result = ImageRgbaU8Impl::readPixel_clamp(source, x, y);
	if (CONVERT_COLOR) {
		result = target.packRgba(ImageRgbaU8Impl::unpackRgba(result, source.packOrder));
	}
	return result;
}

// Used for drawing large pixels
static inline void fillRectangle(ImageRgbaU8Impl& target, int pixelLeft, int pixelRight, int pixelTop, int pixelBottom, const Color4xU8& packedColor) {
	SafePointer<Color4xU8> targetRow = imageInternal::getSafeData<Color4xU8>(target, pixelTop) + pixelLeft;
	for (int y = pixelTop; y < pixelBottom; y++) {
		SafePointer<Color4xU8> targetPixel = targetRow;
		for (int x = pixelLeft; x < pixelRight; x++) {
			*targetPixel = packedColor;
			targetPixel += 1;
		}
		targetRow.increaseBytes(target.stride);
	}
}

template <bool CONVERT_COLOR>
static void blockMagnify_reference(
  ImageRgbaU8Impl& target, const ImageRgbaU8Impl& source,
  int pixelWidth, int pixelHeight, int clipWidth, int clipHeight) {
	int sourceY = 0;
	int maxSourceX = source.width - 1;
	int maxSourceY = source.height - 1;
	if (clipWidth > target.width) { clipWidth = target.width; }
	if (clipHeight > target.height) { clipHeight = target.height; }
	for (int32_t pixelTop = 0; pixelTop < clipHeight; pixelTop += pixelHeight) {
		int sourceX = 0;
		for (int32_t pixelLeft = 0; pixelLeft < clipWidth; pixelLeft += pixelWidth) {
			// Read the pixel once
			Color4xU8 sourceColor = convertRead<CONVERT_COLOR>(target, source, sourceX, sourceY);
			// Write to all target pixels in a conditionless loop
			fillRectangle(target, pixelLeft, pixelLeft + pixelWidth, pixelTop, pixelTop + pixelHeight, sourceColor);
			// Iterate and clamp the read coordinate
			sourceX++;
			if (sourceX > maxSourceX) { sourceX = maxSourceX; }
		}
		// Iterate and clamp the read coordinate
		sourceY++;
		if (sourceY > maxSourceY) { sourceY = maxSourceY; }
	}
}

// Pre-condition:
//   * The source and target images have the same pack order
//   * Both source and target are 16-byte aligned, but does not have to own their padding
//   * clipWidth % 2 == 0
//   * clipHeight % 2 == 0
static void blockMagnify_2x2(ImageRgbaU8Impl& target, const ImageRgbaU8Impl& source, int clipWidth, int clipHeight) {
	#ifdef USE_SIMD_EXTRA
		const SafePointer<uint32_t> sourceRow = imageInternal::getSafeData<uint32_t>(source);
		SafePointer<uint32_t> upperTargetRow = imageInternal::getSafeData<uint32_t>(target, 0);
		SafePointer<uint32_t> lowerTargetRow = imageInternal::getSafeData<uint32_t>(target, 1);
		int doubleTargetStride = target.stride * 2;
		for (int upperTargetY = 0; upperTargetY + 2 <= clipHeight; upperTargetY+=2) {
			// Carriage return
			const SafePointer<uint32_t> sourcePixel = sourceRow;
			SafePointer<uint32_t> upperTargetPixel = upperTargetRow;
			SafePointer<uint32_t> lowerTargetPixel = lowerTargetRow;
			// Write to whole multiples of 8 pixels
			int writeLeftX = 0;
			while (writeLeftX + 8 <= clipWidth) {
				// Read pixels
				ALIGN16 SIMD_U32x4 sourcePixels = U32x4::readAligned(sourcePixel, "blockMagnify_2x2 @ whole sourcePixels").v;
				sourcePixel += 4;
				// Double the pixels by zipping with itself
				ALIGN16 SIMD_U32x4x2 doubledPixels = ZIP_U32_SIMD(sourcePixels, sourcePixels);
				// Write lower part
				U32x4(doubledPixels.val[0]).writeAligned(upperTargetPixel, "blockMagnify_2x2 @ write upper left #1");
				upperTargetPixel += 4;
				U32x4(doubledPixels.val[0]).writeAligned(lowerTargetPixel, "blockMagnify_2x2 @ write lower left #1");
				lowerTargetPixel += 4;
				// Write upper part
				U32x4(doubledPixels.val[1]).writeAligned(upperTargetPixel, "blockMagnify_2x2 @ write upper right #1");
				upperTargetPixel += 4;
				U32x4(doubledPixels.val[1]).writeAligned(lowerTargetPixel, "blockMagnify_2x2 @ write lower right #1");
				lowerTargetPixel += 4;
				// Count
				writeLeftX += 8;
			}
			// Fill the last pixels using scalar operations to avoid going out of bound
			while (writeLeftX + 2 <= clipWidth) {
				// Read one pixel
				uint32_t sourceColor = *sourcePixel;
				// Write 2x2 pixels
				*upperTargetPixel = sourceColor; upperTargetPixel += 1;
				*upperTargetPixel = sourceColor; upperTargetPixel += 1;
				*lowerTargetPixel = sourceColor; lowerTargetPixel += 1;
				*lowerTargetPixel = sourceColor; lowerTargetPixel += 1;
				// Count
				writeLeftX += 2;
			}
			// Line feed
			sourceRow.increaseBytes(source.stride);
			upperTargetRow.increaseBytes(doubleTargetStride);
			lowerTargetRow.increaseBytes(doubleTargetStride);
		}
	#else
		blockMagnify_reference<false>(target, source, 2, 2, clipWidth, clipHeight);
	#endif
}

// Pre-condition:
//   * The source and target images have the same pack order
//   * Both source and target are 16-byte aligned, but does not have to own their padding
//   * clipWidth % 3 == 0
//   * clipHeight % 3 == 0
static void blockMagnify_3x3(ImageRgbaU8Impl& target, const ImageRgbaU8Impl& source, int clipWidth, int clipHeight) {
	const SafePointer<uint32_t> sourceRow = imageInternal::getSafeData<uint32_t>(source);
	SafePointer<uint32_t> targetRowA = imageInternal::getSafeData<uint32_t>(target, 0);
	SafePointer<uint32_t> targetRowB = imageInternal::getSafeData<uint32_t>(target, 1);
	SafePointer<uint32_t> targetRowC = imageInternal::getSafeData<uint32_t>(target, 2);
	int blockTargetStride = target.stride * 3;
	for (int upperTargetY = 0; upperTargetY + 3 <= clipHeight; upperTargetY+=3) {
		// Carriage return
		const SafePointer<uint32_t> sourcePixel = sourceRow;
		SafePointer<uint32_t> targetPixelA = targetRowA;
		SafePointer<uint32_t> targetPixelB = targetRowB;
		SafePointer<uint32_t> targetPixelC = targetRowC;
		int writeLeftX = 0;
		while (writeLeftX + 3 <= clipWidth) {
			// Read one pixel at a time
			uint32_t scalarValue = *sourcePixel;
			sourcePixel += 1;
			// Write to a whole block of pixels
			targetPixelA[0] = scalarValue; targetPixelA[1] = scalarValue; targetPixelA[2] = scalarValue;
			targetPixelB[0] = scalarValue; targetPixelB[1] = scalarValue; targetPixelB[2] = scalarValue;
			targetPixelC[0] = scalarValue; targetPixelC[1] = scalarValue; targetPixelC[2] = scalarValue;
			targetPixelA += 3;
			targetPixelB += 3;
			targetPixelC += 3;
			// Count
			writeLeftX += 3;
		}
		// Line feed
		sourceRow.increaseBytes(source.stride);
		targetRowA.increaseBytes(blockTargetStride);
		targetRowB.increaseBytes(blockTargetStride);
		targetRowC.increaseBytes(blockTargetStride);
	}
}

// Pre-condition:
//   * The source and target images have the same pack order
//   * Both source and target are 16-byte aligned, but does not have to own their padding
//   * clipWidth % 4 == 0
//   * clipHeight % 4 == 0
static void blockMagnify_4x4(ImageRgbaU8Impl& target, const ImageRgbaU8Impl& source, int clipWidth, int clipHeight) {
	const SafePointer<uint32_t> sourceRow = imageInternal::getSafeData<uint32_t>(source);
	SafePointer<uint32_t> targetRowA = imageInternal::getSafeData<uint32_t>(target, 0);
	SafePointer<uint32_t> targetRowB = imageInternal::getSafeData<uint32_t>(target, 1);
	SafePointer<uint32_t> targetRowC = imageInternal::getSafeData<uint32_t>(target, 2);
	SafePointer<uint32_t> targetRowD = imageInternal::getSafeData<uint32_t>(target, 3);
	int quadTargetStride = target.stride * 4;
	for (int upperTargetY = 0; upperTargetY + 4 <= clipHeight; upperTargetY+=4) {
		// Carriage return
		const SafePointer<uint32_t> sourcePixel = sourceRow;
		SafePointer<uint32_t> targetPixelA = targetRowA;
		SafePointer<uint32_t> targetPixelB = targetRowB;
		SafePointer<uint32_t> targetPixelC = targetRowC;
		SafePointer<uint32_t> targetPixelD = targetRowD;
		int writeLeftX = 0;
		while (writeLeftX + 4 <= clipWidth) {
			// Read one pixel at a time
			uint32_t scalarValue = *sourcePixel;
			sourcePixel += 1;
			// Convert scalar to SIMD vector of 4 repeated pixels
			ALIGN16 U32x4 sourcePixels = U32x4(scalarValue);
			// Write to 4x4 pixels using 4 SIMD writes
			sourcePixels.writeAligned(targetPixelA, "blockMagnify_4x4 @ write A");
			sourcePixels.writeAligned(targetPixelB, "blockMagnify_4x4 @ write B");
			sourcePixels.writeAligned(targetPixelC, "blockMagnify_4x4 @ write C");
			sourcePixels.writeAligned(targetPixelD, "blockMagnify_4x4 @ write D");
			targetPixelA += 4;
			targetPixelB += 4;
			targetPixelC += 4;
			targetPixelD += 4;
			// Count
			writeLeftX += 4;
		}
		// Line feed
		sourceRow.increaseBytes(source.stride);
		targetRowA.increaseBytes(quadTargetStride);
		targetRowB.increaseBytes(quadTargetStride);
		targetRowC.increaseBytes(quadTargetStride);
		targetRowD.increaseBytes(quadTargetStride);
	}
}

// Pre-condition:
//   * The source and target images have the same pack order
//   * Both source and target are 16-byte aligned, but does not have to own their padding
//   * clipWidth % 5 == 0
//   * clipHeight % 5 == 0
static void blockMagnify_5x5(ImageRgbaU8Impl& target, const ImageRgbaU8Impl& source, int clipWidth, int clipHeight) {
	const SafePointer<uint32_t> sourceRow = imageInternal::getSafeData<uint32_t>(source);
	SafePointer<uint32_t> targetRowA = imageInternal::getSafeData<uint32_t>(target, 0);
	SafePointer<uint32_t> targetRowB = imageInternal::getSafeData<uint32_t>(target, 1);
	SafePointer<uint32_t> targetRowC = imageInternal::getSafeData<uint32_t>(target, 2);
	SafePointer<uint32_t> targetRowD = imageInternal::getSafeData<uint32_t>(target, 3);
	SafePointer<uint32_t> targetRowE = imageInternal::getSafeData<uint32_t>(target, 4);
	int blockTargetStride = target.stride * 5;
	for (int upperTargetY = 0; upperTargetY + 5 <= clipHeight; upperTargetY+=5) {
		// Carriage return
		const SafePointer<uint32_t> sourcePixel = sourceRow;
		SafePointer<uint32_t> targetPixelA = targetRowA;
		SafePointer<uint32_t> targetPixelB = targetRowB;
		SafePointer<uint32_t> targetPixelC = targetRowC;
		SafePointer<uint32_t> targetPixelD = targetRowD;
		SafePointer<uint32_t> targetPixelE = targetRowE;
		int writeLeftX = 0;
		while (writeLeftX + 5 <= clipWidth) {
			// Read one pixel at a time
			uint32_t scalarValue = *sourcePixel;
			sourcePixel += 1;
			// Write to a whole block of pixels
			targetPixelA[0] = scalarValue; targetPixelA[1] = scalarValue; targetPixelA[2] = scalarValue; targetPixelA[3] = scalarValue; targetPixelA[4] = scalarValue;
			targetPixelB[0] = scalarValue; targetPixelB[1] = scalarValue; targetPixelB[2] = scalarValue; targetPixelB[3] = scalarValue; targetPixelB[4] = scalarValue;
			targetPixelC[0] = scalarValue; targetPixelC[1] = scalarValue; targetPixelC[2] = scalarValue; targetPixelC[3] = scalarValue; targetPixelC[4] = scalarValue;
			targetPixelD[0] = scalarValue; targetPixelD[1] = scalarValue; targetPixelD[2] = scalarValue; targetPixelD[3] = scalarValue; targetPixelD[4] = scalarValue;
			targetPixelE[0] = scalarValue; targetPixelE[1] = scalarValue; targetPixelE[2] = scalarValue; targetPixelE[3] = scalarValue; targetPixelE[4] = scalarValue;
			targetPixelA += 5;
			targetPixelB += 5;
			targetPixelC += 5;
			targetPixelD += 5;
			targetPixelE += 5;
			// Count
			writeLeftX += 5;
		}
		// Line feed
		sourceRow.increaseBytes(source.stride);
		targetRowA.increaseBytes(blockTargetStride);
		targetRowB.increaseBytes(blockTargetStride);
		targetRowC.increaseBytes(blockTargetStride);
		targetRowD.increaseBytes(blockTargetStride);
		targetRowE.increaseBytes(blockTargetStride);
	}
}

// Pre-condition:
//   * The source and target images have the same pack order
//   * Both source and target are 16-byte aligned, but does not have to own their padding
//   * clipWidth % 6 == 0
//   * clipHeight % 6 == 0
static void blockMagnify_6x6(ImageRgbaU8Impl& target, const ImageRgbaU8Impl& source, int clipWidth, int clipHeight) {
	const SafePointer<uint32_t> sourceRow = imageInternal::getSafeData<uint32_t>(source);
	SafePointer<uint32_t> targetRowA = imageInternal::getSafeData<uint32_t>(target, 0);
	SafePointer<uint32_t> targetRowB = imageInternal::getSafeData<uint32_t>(target, 1);
	SafePointer<uint32_t> targetRowC = imageInternal::getSafeData<uint32_t>(target, 2);
	SafePointer<uint32_t> targetRowD = imageInternal::getSafeData<uint32_t>(target, 3);
	SafePointer<uint32_t> targetRowE = imageInternal::getSafeData<uint32_t>(target, 4);
	SafePointer<uint32_t> targetRowF = imageInternal::getSafeData<uint32_t>(target, 5);
	int blockTargetStride = target.stride * 6;
	for (int upperTargetY = 0; upperTargetY + 6 <= clipHeight; upperTargetY+=6) {
		// Carriage return
		const SafePointer<uint32_t> sourcePixel = sourceRow;
		SafePointer<uint32_t> targetPixelA = targetRowA;
		SafePointer<uint32_t> targetPixelB = targetRowB;
		SafePointer<uint32_t> targetPixelC = targetRowC;
		SafePointer<uint32_t> targetPixelD = targetRowD;
		SafePointer<uint32_t> targetPixelE = targetRowE;
		SafePointer<uint32_t> targetPixelF = targetRowF;
		int writeLeftX = 0;
		while (writeLeftX + 6 <= clipWidth) {
			// Read one pixel at a time
			uint32_t scalarValue = *sourcePixel;
			sourcePixel += 1;
			// Write to a whole block of pixels
			targetPixelA[0] = scalarValue; targetPixelA[1] = scalarValue; targetPixelA[2] = scalarValue; targetPixelA[3] = scalarValue; targetPixelA[4] = scalarValue; targetPixelA[5] = scalarValue;
			targetPixelB[0] = scalarValue; targetPixelB[1] = scalarValue; targetPixelB[2] = scalarValue; targetPixelB[3] = scalarValue; targetPixelB[4] = scalarValue; targetPixelB[5] = scalarValue;
			targetPixelC[0] = scalarValue; targetPixelC[1] = scalarValue; targetPixelC[2] = scalarValue; targetPixelC[3] = scalarValue; targetPixelC[4] = scalarValue; targetPixelC[5] = scalarValue;
			targetPixelD[0] = scalarValue; targetPixelD[1] = scalarValue; targetPixelD[2] = scalarValue; targetPixelD[3] = scalarValue; targetPixelD[4] = scalarValue; targetPixelD[5] = scalarValue;
			targetPixelE[0] = scalarValue; targetPixelE[1] = scalarValue; targetPixelE[2] = scalarValue; targetPixelE[3] = scalarValue; targetPixelE[4] = scalarValue; targetPixelE[5] = scalarValue;
			targetPixelF[0] = scalarValue; targetPixelF[1] = scalarValue; targetPixelF[2] = scalarValue; targetPixelF[3] = scalarValue; targetPixelF[4] = scalarValue; targetPixelF[5] = scalarValue;
			targetPixelA += 6;
			targetPixelB += 6;
			targetPixelC += 6;
			targetPixelD += 6;
			targetPixelE += 6;
			targetPixelF += 6;
			// Count
			writeLeftX += 6;
		}
		// Line feed
		sourceRow.increaseBytes(source.stride);
		targetRowA.increaseBytes(blockTargetStride);
		targetRowB.increaseBytes(blockTargetStride);
		targetRowC.increaseBytes(blockTargetStride);
		targetRowD.increaseBytes(blockTargetStride);
		targetRowE.increaseBytes(blockTargetStride);
		targetRowF.increaseBytes(blockTargetStride);
	}
}

// Pre-condition:
//   * The source and target images have the same pack order
//   * Both source and target are 16-byte aligned, but does not have to own their padding
//   * clipWidth % 7 == 0
//   * clipHeight % 7 == 0
static void blockMagnify_7x7(ImageRgbaU8Impl& target, const ImageRgbaU8Impl& source, int clipWidth, int clipHeight) {
	const SafePointer<uint32_t> sourceRow = imageInternal::getSafeData<uint32_t>(source);
	SafePointer<uint32_t> targetRowA = imageInternal::getSafeData<uint32_t>(target, 0);
	SafePointer<uint32_t> targetRowB = imageInternal::getSafeData<uint32_t>(target, 1);
	SafePointer<uint32_t> targetRowC = imageInternal::getSafeData<uint32_t>(target, 2);
	SafePointer<uint32_t> targetRowD = imageInternal::getSafeData<uint32_t>(target, 3);
	SafePointer<uint32_t> targetRowE = imageInternal::getSafeData<uint32_t>(target, 4);
	SafePointer<uint32_t> targetRowF = imageInternal::getSafeData<uint32_t>(target, 5);
	SafePointer<uint32_t> targetRowG = imageInternal::getSafeData<uint32_t>(target, 6);
	int blockTargetStride = target.stride * 7;
	for (int upperTargetY = 0; upperTargetY + 7 <= clipHeight; upperTargetY+=7) {
		// Carriage return
		const SafePointer<uint32_t> sourcePixel = sourceRow;
		SafePointer<uint32_t> targetPixelA = targetRowA;
		SafePointer<uint32_t> targetPixelB = targetRowB;
		SafePointer<uint32_t> targetPixelC = targetRowC;
		SafePointer<uint32_t> targetPixelD = targetRowD;
		SafePointer<uint32_t> targetPixelE = targetRowE;
		SafePointer<uint32_t> targetPixelF = targetRowF;
		SafePointer<uint32_t> targetPixelG = targetRowG;
		int writeLeftX = 0;
		while (writeLeftX + 7 <= clipWidth) {
			// Read one pixel at a time
			uint32_t scalarValue = *sourcePixel;
			sourcePixel += 1;
			// Write to a whole block of pixels
			targetPixelA[0] = scalarValue; targetPixelA[1] = scalarValue; targetPixelA[2] = scalarValue; targetPixelA[3] = scalarValue; targetPixelA[4] = scalarValue; targetPixelA[5] = scalarValue; targetPixelA[6] = scalarValue;
			targetPixelB[0] = scalarValue; targetPixelB[1] = scalarValue; targetPixelB[2] = scalarValue; targetPixelB[3] = scalarValue; targetPixelB[4] = scalarValue; targetPixelB[5] = scalarValue; targetPixelB[6] = scalarValue;
			targetPixelC[0] = scalarValue; targetPixelC[1] = scalarValue; targetPixelC[2] = scalarValue; targetPixelC[3] = scalarValue; targetPixelC[4] = scalarValue; targetPixelC[5] = scalarValue; targetPixelC[6] = scalarValue;
			targetPixelD[0] = scalarValue; targetPixelD[1] = scalarValue; targetPixelD[2] = scalarValue; targetPixelD[3] = scalarValue; targetPixelD[4] = scalarValue; targetPixelD[5] = scalarValue; targetPixelD[6] = scalarValue;
			targetPixelE[0] = scalarValue; targetPixelE[1] = scalarValue; targetPixelE[2] = scalarValue; targetPixelE[3] = scalarValue; targetPixelE[4] = scalarValue; targetPixelE[5] = scalarValue; targetPixelE[6] = scalarValue;
			targetPixelF[0] = scalarValue; targetPixelF[1] = scalarValue; targetPixelF[2] = scalarValue; targetPixelF[3] = scalarValue; targetPixelF[4] = scalarValue; targetPixelF[5] = scalarValue; targetPixelF[6] = scalarValue;
			targetPixelG[0] = scalarValue; targetPixelG[1] = scalarValue; targetPixelG[2] = scalarValue; targetPixelG[3] = scalarValue; targetPixelG[4] = scalarValue; targetPixelG[5] = scalarValue; targetPixelG[6] = scalarValue;
			targetPixelA += 7;
			targetPixelB += 7;
			targetPixelC += 7;
			targetPixelD += 7;
			targetPixelE += 7;
			targetPixelF += 7;
			targetPixelG += 7;
			// Count
			writeLeftX += 7;
		}
		// Line feed
		sourceRow.increaseBytes(source.stride);
		targetRowA.increaseBytes(blockTargetStride);
		targetRowB.increaseBytes(blockTargetStride);
		targetRowC.increaseBytes(blockTargetStride);
		targetRowD.increaseBytes(blockTargetStride);
		targetRowE.increaseBytes(blockTargetStride);
		targetRowF.increaseBytes(blockTargetStride);
		targetRowG.increaseBytes(blockTargetStride);
	}
}

// Pre-condition:
//   * The source and target images have the same pack order
//   * Both source and target are 16-byte aligned, but does not have to own their padding
//   * clipWidth % 8 == 0
//   * clipHeight % 8 == 0
static void blockMagnify_8x8(ImageRgbaU8Impl& target, const ImageRgbaU8Impl& source, int clipWidth, int clipHeight) {
	const SafePointer<uint32_t> sourceRow = imageInternal::getSafeData<uint32_t>(source);
	SafePointer<uint32_t> targetRowA = imageInternal::getSafeData<uint32_t>(target, 0);
	SafePointer<uint32_t> targetRowB = imageInternal::getSafeData<uint32_t>(target, 1);
	SafePointer<uint32_t> targetRowC = imageInternal::getSafeData<uint32_t>(target, 2);
	SafePointer<uint32_t> targetRowD = imageInternal::getSafeData<uint32_t>(target, 3);
	SafePointer<uint32_t> targetRowE = imageInternal::getSafeData<uint32_t>(target, 4);
	SafePointer<uint32_t> targetRowF = imageInternal::getSafeData<uint32_t>(target, 5);
	SafePointer<uint32_t> targetRowG = imageInternal::getSafeData<uint32_t>(target, 6);
	SafePointer<uint32_t> targetRowH = imageInternal::getSafeData<uint32_t>(target, 7);
	int blockTargetStride = target.stride * 8;
	for (int upperTargetY = 0; upperTargetY + 8 <= clipHeight; upperTargetY+=8) {
		// Carriage return
		const SafePointer<uint32_t> sourcePixel = sourceRow;
		SafePointer<uint32_t> targetPixelA = targetRowA;
		SafePointer<uint32_t> targetPixelB = targetRowB;
		SafePointer<uint32_t> targetPixelC = targetRowC;
		SafePointer<uint32_t> targetPixelD = targetRowD;
		SafePointer<uint32_t> targetPixelE = targetRowE;
		SafePointer<uint32_t> targetPixelF = targetRowF;
		SafePointer<uint32_t> targetPixelG = targetRowG;
		SafePointer<uint32_t> targetPixelH = targetRowH;
		int writeLeftX = 0;
		while (writeLeftX + 8 <= clipWidth) {
			// Read one pixel at a time
			uint32_t scalarValue = *sourcePixel;
			sourcePixel += 1;
			// Write to a whole block of pixels
			targetPixelA[0] = scalarValue; targetPixelA[1] = scalarValue; targetPixelA[2] = scalarValue; targetPixelA[3] = scalarValue; targetPixelA[4] = scalarValue; targetPixelA[5] = scalarValue; targetPixelA[6] = scalarValue; targetPixelA[7] = scalarValue;
			targetPixelB[0] = scalarValue; targetPixelB[1] = scalarValue; targetPixelB[2] = scalarValue; targetPixelB[3] = scalarValue; targetPixelB[4] = scalarValue; targetPixelB[5] = scalarValue; targetPixelB[6] = scalarValue; targetPixelB[7] = scalarValue;
			targetPixelC[0] = scalarValue; targetPixelC[1] = scalarValue; targetPixelC[2] = scalarValue; targetPixelC[3] = scalarValue; targetPixelC[4] = scalarValue; targetPixelC[5] = scalarValue; targetPixelC[6] = scalarValue; targetPixelC[7] = scalarValue;
			targetPixelD[0] = scalarValue; targetPixelD[1] = scalarValue; targetPixelD[2] = scalarValue; targetPixelD[3] = scalarValue; targetPixelD[4] = scalarValue; targetPixelD[5] = scalarValue; targetPixelD[6] = scalarValue; targetPixelD[7] = scalarValue;
			targetPixelE[0] = scalarValue; targetPixelE[1] = scalarValue; targetPixelE[2] = scalarValue; targetPixelE[3] = scalarValue; targetPixelE[4] = scalarValue; targetPixelE[5] = scalarValue; targetPixelE[6] = scalarValue; targetPixelE[7] = scalarValue;
			targetPixelF[0] = scalarValue; targetPixelF[1] = scalarValue; targetPixelF[2] = scalarValue; targetPixelF[3] = scalarValue; targetPixelF[4] = scalarValue; targetPixelF[5] = scalarValue; targetPixelF[6] = scalarValue; targetPixelF[7] = scalarValue;
			targetPixelG[0] = scalarValue; targetPixelG[1] = scalarValue; targetPixelG[2] = scalarValue; targetPixelG[3] = scalarValue; targetPixelG[4] = scalarValue; targetPixelG[5] = scalarValue; targetPixelG[6] = scalarValue; targetPixelG[7] = scalarValue;
			targetPixelH[0] = scalarValue; targetPixelH[1] = scalarValue; targetPixelH[2] = scalarValue; targetPixelH[3] = scalarValue; targetPixelH[4] = scalarValue; targetPixelH[5] = scalarValue; targetPixelH[6] = scalarValue; targetPixelH[7] = scalarValue;
			targetPixelA += 8;
			targetPixelB += 8;
			targetPixelC += 8;
			targetPixelD += 8;
			targetPixelE += 8;
			targetPixelF += 8;
			targetPixelG += 8;
			targetPixelH += 8;
			// Count
			writeLeftX += 8;
		}
		// Line feed
		sourceRow.increaseBytes(source.stride);
		targetRowA.increaseBytes(blockTargetStride);
		targetRowB.increaseBytes(blockTargetStride);
		targetRowC.increaseBytes(blockTargetStride);
		targetRowD.increaseBytes(blockTargetStride);
		targetRowE.increaseBytes(blockTargetStride);
		targetRowF.increaseBytes(blockTargetStride);
		targetRowG.increaseBytes(blockTargetStride);
		targetRowH.increaseBytes(blockTargetStride);
	}
}

static void blackEdges(ImageRgbaU8Impl& target, int excludedWidth, int excludedHeight) {
	// Right side
	drawSolidRectangleMemset<Color4xU8>(target, excludedWidth, 0, target.width, excludedHeight, 0);
	// Bottom and corner
	drawSolidRectangleMemset<Color4xU8>(target, 0, excludedHeight, target.width, target.height, 0);
}

void dsr::imageImpl_blockMagnify(ImageRgbaU8Impl& target, const ImageRgbaU8Impl& source, int pixelWidth, int pixelHeight) {
	if (pixelWidth < 1) { pixelWidth = 1; }
	if (pixelHeight < 1) { pixelHeight = 1; }
	bool sameOrder = target.packOrder.packOrderIndex == source.packOrder.packOrderIndex;
	// Find the part of source which fits into target with whole pixels
	int clipWidth = roundDown(std::min(target.width, source.width * pixelWidth), pixelWidth);
	int clipHeight = roundDown(std::min(target.height, source.height * pixelHeight), pixelHeight);
	if (sameOrder) {
		if (imageIs16ByteAligned(source) && imageIs16ByteAligned(target)) {
			if (pixelWidth == 2 && pixelHeight == 2) {
				blockMagnify_2x2(target, source, clipWidth, clipHeight);
			} else if (pixelWidth == 3 && pixelHeight == 3) {
				blockMagnify_3x3(target, source, clipWidth, clipHeight);
			} else if (pixelWidth == 4 && pixelHeight == 4) {
				blockMagnify_4x4(target, source, clipWidth, clipHeight);
			} else if (pixelWidth == 5 && pixelHeight == 5) {
				blockMagnify_5x5(target, source, clipWidth, clipHeight);
			} else if (pixelWidth == 6 && pixelHeight == 6) {
				blockMagnify_6x6(target, source, clipWidth, clipHeight);
			} else if (pixelWidth == 7 && pixelHeight == 7) {
				blockMagnify_7x7(target, source, clipWidth, clipHeight);
			} else if (pixelWidth == 8 && pixelHeight == 8) {
				blockMagnify_8x8(target, source, clipWidth, clipHeight);
			} else {
				blockMagnify_reference<false>(target, source, pixelWidth, pixelHeight, clipWidth, clipHeight);
			}
		} else {
			blockMagnify_reference<false>(target, source, pixelWidth, pixelHeight, clipWidth, clipHeight);
		}
	} else {
		blockMagnify_reference<true>(target, source, pixelWidth, pixelHeight, clipWidth, clipHeight);
	}
	blackEdges(target, clipWidth, clipHeight);
}
