﻿#include "stdafx.h"
#include "gmgltexture.h"
#include "gmdatacore/imagereader/imagereader.h"
#include "shader_constants.h"

GMGLTexture::GMGLTexture(AUTORELEASE Image* image)
{
	m_image.reset(image);
}

GMGLTexture::~GMGLTexture()
{
	glDeleteTextures(1, &m_id);
}

void GMGLTexture::init()
{
	GMuint level;
	const ImageData& image = m_image->getData();

	glGenTextures(1, &m_id);
	glBindTexture(image.target, m_id);

	switch (image.target)
	{
	case GL_TEXTURE_1D:
		glTexStorage1D(image.target,
			image.mipLevels,
			image.internalFormat,
			image.mip[0].width);
		for (level = 0; level < image.mipLevels; ++level)
		{
			glTexSubImage1D(GL_TEXTURE_1D,
				level,
				0,
				image.mip[level].width,
				image.format, image.type,
				image.mip[level].data);
		}
		break;
	case GL_TEXTURE_1D_ARRAY:
		glTexStorage2D(image.target,
			image.mipLevels,
			image.internalFormat,
			image.mip[0].width,
			image.slices);
		for (level = 0; level < image.mipLevels; ++level)
		{
			glTexSubImage2D(GL_TEXTURE_1D,
				level,
				0, 0,
				image.mip[level].width, image.slices,
				image.format, image.type,
				image.mip[level].data);
		}
		break;
	case GL_TEXTURE_2D:
		glTexStorage2D(image.target,
			image.mipLevels,
			image.internalFormat,
			image.mip[0].width,
			image.mip[0].height);
		for (level = 0; level < image.mipLevels; ++level)
		{
			glTexSubImage2D(GL_TEXTURE_2D,
				level,
				0, 0,
				image.mip[level].width, image.mip[level].height,
				image.format, image.type,
				image.mip[level].data);
		}
		break;
	case GL_TEXTURE_CUBE_MAP:
		for (level = 0; level < image.mipLevels; ++level)
		{
			GMbyte* ptr = (GMbyte *)image.mip[level].data;
			for (int face = 0; face < 6; face++)
			{
				glTexImage2D(GL_TEXTURE_CUBE_MAP_POSITIVE_X + face,
					level,
					image.internalFormat,
					image.mip[level].width, image.mip[level].height,
					0,
					image.format, image.type,
					ptr + image.sliceStride * face);
			}
		}
		break;
	case GL_TEXTURE_2D_ARRAY:
		glTexStorage3D(image.target,
			image.mipLevels,
			image.internalFormat,
			image.mip[0].width,
			image.mip[0].height,
			image.slices);
		for (level = 0; level < image.mipLevels; ++level)
		{
			glTexSubImage3D(GL_TEXTURE_2D_ARRAY,
				level,
				0, 0, 0,
				image.mip[level].width, image.mip[level].height, image.slices,
				image.format, image.type,
				image.mip[level].data);
		}
		break;
	case GL_TEXTURE_CUBE_MAP_ARRAY:
		glTexStorage3D(image.target,
			image.mipLevels,
			image.internalFormat,
			image.mip[0].width,
			image.mip[0].height,
			image.slices);
		break;
	case GL_TEXTURE_3D:
		glTexStorage3D(image.target,
			image.mipLevels,
			image.internalFormat,
			image.mip[0].width,
			image.mip[0].height,
			image.mip[0].depth);
		for (level = 0; level < image.mipLevels; ++level)
		{
			glTexSubImage3D(GL_TEXTURE_3D,
				level,
				0, 0, 0,
				image.mip[level].width, image.mip[level].height, image.mip[level].depth,
				image.format, image.type,
				image.mip[level].data);
		}
		break;
	default:
		break;
	}

	glTexParameteriv(image.target, GL_TEXTURE_SWIZZLE_RGBA, reinterpret_cast<const GLint *>(image.swizzle));
	
	m_image->dispose();

	glBindTexture(image.target, 0);
}

void GMGLTexture::beginTexture()
{
	const ImageData& image = m_image->getData();
	glActiveTexture(GMTEXTURE_AMBIENT);
	glBindTexture(image.target, m_id);
}

void GMGLTexture::endTexture()
{
	const ImageData& image = m_image->getData();
	glBindTexture(image.target, 0);
}
