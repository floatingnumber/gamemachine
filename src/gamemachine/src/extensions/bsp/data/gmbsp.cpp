﻿#include "stdafx.h"
#ifdef __APPLE__
#include <stdlib.h>
#endif
#include "foundation/defines.h"
#include "gmbsp.h"
#include <stdio.h>
#include "foundation/utilities/tools.h"
#include "gmdata/gamepackage/gmgamepackage.h"
#include <linearmath.h>

BEGIN_NS

namespace
{
	const char* getValue(const GMBSPEntity* entity, const char* key)
	{
		GMBSPEPair* e = entity->epairs;
		while (e)
		{
			if (e->key == key)
				return e->value.toStdString().c_str();
			e = e->next;
		}
		return nullptr;
	}

	GMint32 copyLump(GMBSPHeader* header, GMint32 lump, void *dest, GMint32 size)
	{
		GMint32 length, ofs;

		length = header->lumps[lump].filelen;
		ofs = header->lumps[lump].fileofs;

		if (length % size)
			gm_error(gm_dbg_wrap("LoadBSPFile: odd lump size"));

		memcpy(dest, (GMbyte *)header + ofs, length);

		return length / size;
	}

	char* copyString(const char *s)
	{
		char *b;
		GMsize_t len = strlen(s) + 1;
		b = (char*)malloc(len);
		GMString::stringCopy(b, len, s);
		return b;
	}

	void stripTrailing(GMString& e)
	{
		std::wstring wstr = e.toStdWString();
		for (auto& c : wstr)
		{
			if (c <= 32)
				c = 0;
		}
		e = wstr;
	}

	GMString expandPath(const char *path)
	{
		GMString strPath = GMPath::getCurrentPath();
		strPath.append(path);
		return strPath;
	}

	void safeRead(FILE *f, void *buffer, GMint32 count)
	{
		if (fread(buffer, 1, count, f) != (size_t)count)
			gm_error(gm_dbg_wrap("File read failure"));
	}

	FILE *safeOpenRead(const char *filename)
	{
		FILE *f = nullptr;

		fopen_s(&f, filename, "rb");

		if (!f)
			gm_error(gm_dbg_wrap("Error opening {0}."), filename );

		return f;
	}

	GMint32 filelength(FILE *f)
	{
		GMint32 pos;
		GMint32 end;

		pos = ftell(f);
		fseek(f, 0, SEEK_END);
		end = ftell(f);
		fseek(f, pos, SEEK_SET);

		return end;
	}

	GMint32 loadFile(const char *filename, void **bufferptr)
	{
		FILE *f;
		GMint32 length;
		void* buffer;

		f = safeOpenRead(filename);
		length = filelength(f);
		buffer = malloc(length + 1);
		((char *)buffer)[length] = 0;
		safeRead(f, buffer, length);
		fclose(f);

		*bufferptr = buffer;
		return length;
	}
}

#define Copy(dest, src) memcpy(dest, src, sizeof(src))
#define CopyMember(dest, src, prop) dest.prop = src.prop

//////////////////////////////////////////////////////////////////////////
GMBSP::GMBSP()
{
	GM_CREATE_DATA();
}

GMBSP::~GMBSP()
{
	D(d);
	for (auto& entity : d->entities)
	{
		GM_delete(entity);
	}
}

void GMBSP::loadBsp(const GMBuffer& buf)
{
	D(d);
	d->buffer = &buf;
	swapBsp();
	parseEntities();
	toDxCoord();
	generateLightVolumes();
}

GMBSPData& GMBSP::bspData()
{
	D(d);
	return *d;
}

void GMBSP::swapBsp()
{
	D(d);
	d->header = (GMBSPHeader*)d->buffer->getData();

	if (d->header->ident != BSP_IDENT) {
		gm_error(gm_dbg_wrap("Invalid IBSP file"));
	}
	if (d->header->version != BSP_VERSION) {
		gm_error(gm_dbg_wrap("Bad version of bsp."));
	}

	// 读取无对齐结构
	loadNoAlignData();

	// 以下结构有对齐，应该用特殊方式读取
	loadPlanes();
	loadVertices();
	loadDrawSurfaces();
}

void GMBSP::loadPlanes()
{
	D(d);
	struct __Tag
	{
		GMfloat p[4];
	};

	GMint32 length = (d->header->lumps[LUMP_PLANES].filelen) / sizeof(__Tag);
	if (length > 0)
	{
		AlignedVector<__Tag> t;
		t.resize(length);
		GMint32 num = copyLump(d->header, LUMP_PLANES, &t[0], sizeof(__Tag));
		d->numplanes = num;
		d->planes.resize(length);
		for (GMint32 i = 0; i < num; i++)
		{
			d->planes[i] = GMVec4(t[i].p[0], t[i].p[1], t[i].p[2], t[i].p[3]);
		}
	}
	else
	{
		d->numplanes = 0;
	}
}

void GMBSP::loadVertices()
{
	D(d);
	struct __Tag
	{
		GMfloat xyz[3];
		GMfloat st[2];
		GMfloat lightmap[2];
		GMfloat normal[3];
		GMbyte color[4];
	};

	GMint32 length = (d->header->lumps[LUMP_DRAWVERTS].filelen) / sizeof(__Tag);
	if (length > 0)
	{
		AlignedVector<__Tag> t;
		t.resize(length);
		GMint32 num = copyLump(d->header, LUMP_DRAWVERTS, &t[0], sizeof(__Tag));
		d->numDrawVertices = num;
		d->vertices.resize(length);
		for (GMint32 i = 0; i < num; i++)
		{
			d->vertices[i].xyz = GMVec3(t[i].xyz[0], t[i].xyz[1], t[i].xyz[2]);
			d->vertices[i].normal = GMVec3(t[i].normal[0], t[i].normal[1], t[i].normal[2]);
			Copy(d->vertices[i].st, t[i].st);
			Copy(d->vertices[i].color, t[i].color);
			Copy(d->vertices[i].lightmap, t[i].lightmap);
		}
	}
	else
	{
		d->numDrawVertices = 0;
	}
}

void GMBSP::loadDrawSurfaces()
{
	D(d);
	struct __Tag
	{
		GMint32 shaderNum;
		GMint32 fogNum;
		GMint32 surfaceType;

		GMint32 firstVert;
		GMint32 numVerts;

		GMint32 firstIndex;
		GMint32 numIndexes;

		GMint32 lightmapNum;
		GMint32 lightmapX, lightmapY;
		GMint32 lightmapWidth, lightmapHeight;

		GMfloat lightmapOrigin[3];
		GMfloat lightmapVecs[3][3];

		GMint32 patchWidth;
		GMint32 patchHeight;
	};

	GMint32 length = (d->header->lumps[LUMP_SURFACES].filelen) / sizeof(__Tag);
	if (length > 0)
	{
		AlignedVector<__Tag> t;
		t.resize(length);
		GMint32 num = copyLump(d->header, LUMP_SURFACES, &t[0], sizeof(__Tag));
		d->numDrawSurfaces = num;
		d->drawSurfaces.resize(length);
		for (GMint32 i = 0; i < num; i++)
		{
			d->drawSurfaces[i].lightmapOrigin = GMVec3(t[i].lightmapOrigin[0], t[i].lightmapOrigin[1], t[i].lightmapOrigin[2]);
			for (GMint32 j = 0; j < 3; j++)
			{
				d->drawSurfaces[i].lightmapVecs[j] = GMVec3(t[i].lightmapVecs[j][0], t[i].lightmapVecs[j][1], t[i].lightmapVecs[j][2]);
			}
			CopyMember(d->drawSurfaces[i], t[i], shaderNum);
			CopyMember(d->drawSurfaces[i], t[i], fogNum);
			CopyMember(d->drawSurfaces[i], t[i], surfaceType);

			CopyMember(d->drawSurfaces[i], t[i], firstVert);
			CopyMember(d->drawSurfaces[i], t[i], numVerts);

			CopyMember(d->drawSurfaces[i], t[i], firstIndex);
			CopyMember(d->drawSurfaces[i], t[i], numIndexes);

			CopyMember(d->drawSurfaces[i], t[i], lightmapNum);
			CopyMember(d->drawSurfaces[i], t[i], lightmapX);
			CopyMember(d->drawSurfaces[i], t[i], lightmapY);
			CopyMember(d->drawSurfaces[i], t[i], lightmapWidth);
			CopyMember(d->drawSurfaces[i], t[i], lightmapHeight);

			CopyMember(d->drawSurfaces[i], t[i], patchWidth);
			CopyMember(d->drawSurfaces[i], t[i], patchHeight);
		}
	}
	else
	{
		d->numDrawSurfaces = 0;
	}
}

void GMBSP::loadNoAlignData()
{
	D(d);
	const int extrasize = 1;
	GMint32 length = (d->header->lumps[LUMP_SHADERS].filelen) / sizeof(GMBSPShader);
	d->shaders.resize(length + extrasize);
	d->numShaders = copyLump(d->header, LUMP_SHADERS, &d->shaders[0], sizeof(GMBSPShader));

	length = (d->header->lumps[LUMP_MODELS].filelen) / sizeof(GMBSPModel);
	d->models.resize(length + extrasize);
	d->nummodels = copyLump(d->header, LUMP_MODELS, &d->models[0], sizeof(GMBSPModel));

	length = (d->header->lumps[LUMP_LEAFS].filelen) / sizeof(GMBSPLeaf);
	d->leafs.resize(length + extrasize);
	d->numleafs = copyLump(d->header, LUMP_LEAFS, &d->leafs[0], sizeof(GMBSPLeaf));

	length = (d->header->lumps[LUMP_NODES].filelen) / sizeof(GMBSPNode);
	d->nodes.resize(length + extrasize);
	d->numnodes = copyLump(d->header, LUMP_NODES, &d->nodes[0], sizeof(GMBSPNode));

	length = (d->header->lumps[LUMP_LEAFSURFACES].filelen) / sizeof(int);
	d->leafsurfaces.resize(length + extrasize);
	d->numleafsurfaces = copyLump(d->header, LUMP_LEAFSURFACES, &d->leafsurfaces[0], sizeof(int));

	length = (d->header->lumps[LUMP_LEAFBRUSHES].filelen) / sizeof(int);
	d->leafbrushes.resize(length + extrasize);
	d->numleafbrushes = copyLump(d->header, LUMP_LEAFBRUSHES, &d->leafbrushes[0], sizeof(int));

	length = (d->header->lumps[LUMP_BRUSHES].filelen) / sizeof(GMBSPBrush);
	d->brushes.resize(length + extrasize);
	d->numbrushes = copyLump(d->header, LUMP_BRUSHES, &d->brushes[0], sizeof(GMBSPBrush));

	length = (d->header->lumps[LUMP_BRUSHSIDES].filelen) / sizeof(GMBSPBrushSide);
	d->brushsides.resize(length + extrasize);
	d->numbrushsides = copyLump(d->header, LUMP_BRUSHSIDES, &d->brushsides[0], sizeof(GMBSPBrushSide));

	length = (d->header->lumps[LUMP_FOGS].filelen) / sizeof(GMBSPFog);
	d->fogs.resize(length + extrasize);
	d->numFogs = copyLump(d->header, LUMP_FOGS, &d->fogs[0], sizeof(GMBSPFog));

	length = (d->header->lumps[LUMP_DRAWINDEXES].filelen) / sizeof(int);
	d->drawIndexes.resize(length + extrasize);
	d->numDrawIndexes = copyLump(d->header, LUMP_DRAWINDEXES, &d->drawIndexes[0], sizeof(int));

	length = (d->header->lumps[LUMP_VISIBILITY].filelen) / 1;
	d->visBytes.resize(length + extrasize);
	d->numVisBytes = copyLump(d->header, LUMP_VISIBILITY, &d->visBytes[0], 1);

	length = (d->header->lumps[LUMP_LIGHTMAPS].filelen) / 1;
	d->lightBytes.resize(length + extrasize);
	d->numLightBytes = copyLump(d->header, LUMP_LIGHTMAPS, &d->lightBytes[0], 1);

	length = (d->header->lumps[LUMP_ENTITIES].filelen) / 1;
	d->entdata.resize(length + extrasize);
	d->entdatasize = copyLump(d->header, LUMP_ENTITIES, &d->entdata[0], 1);

	length = (d->header->lumps[LUMP_LIGHTGRID].filelen) / 1;
	d->gridData.resize(length + extrasize);
	d->numGridPoints = copyLump(d->header, LUMP_LIGHTGRID, &d->gridData[0], 8);
}

// 将坐标系转化为左手坐标系(DirectX)
// Quake的坐标系为：z正向朝上，y正向朝内，x正向朝右
void GMBSP::toDxCoord()
{
	D(d);
	for (GMint32 i = 0; i < d->numDrawVertices; i++)
	{
		//swap y and z
		GMfloat _y = d->vertices[i].xyz.getY();
		d->vertices[i].xyz.setY(d->vertices[i].xyz.getZ());
		d->vertices[i].xyz.setZ(_y);
	}

	for (GMint32 i = 0; i < d->numplanes; ++i)
	{
		GMVec3 n = d->planes[i].getNormal();
		GMfloat _y = n.getY();
		n.setY(n.getZ());
		n.setZ(_y);
		d->planes[i].setNormal(n);
		GM_ASSERT(Fabs(Length(d->planes[i].getNormal()) - 1) < 0.01);

		d->planes[i].setIntercept(-d->planes[i].getIntercept());
	}
}

void GMBSP::parseEntities()
{
	D(d);
	d->lightVols.lightVolSize[0] = 64;
	d->lightVols.lightVolSize[1] = 64;
	d->lightVols.lightVolSize[2] = 128;

	parseFromMemory(d->entdata.data(), d->entdatasize);

	GMBSPEntity* entity;
	while (parseEntity(&entity))
	{
		d->entities.push_back(entity);
		const char* gridSize = getValue(entity, "gridsize");
		if (gridSize)
		{
			GMScanner s(gridSize);
			s.nextFloat(d->lightVols.lightVolSize[0]);
			s.nextFloat(d->lightVols.lightVolSize[1]);
			s.nextFloat(d->lightVols.lightVolSize[2]);
			gm_info(gm_dbg_wrap("gridSize reseted to {0}, {1}, {2}"), GMString(d->lightVols.lightVolSize[0]), GMString(d->lightVols.lightVolSize[1]), GMString(d->lightVols.lightVolSize[2]));
		}
	}
}

void GMBSP::generateLightVolumes()
{
	D(d);
	for (GMuint32 i = 0; i < 3; ++i)
	{
		d->lightVols.lightVolInverseSize[i] = 1.f / d->lightVols.lightVolSize[i];
	}
	GMfloat* wMins = d->models[0].mins;
	GMfloat* wMaxs = d->models[0].maxs;
	GMfloat maxs[3];
	GMint32 numGridPoints = 0;

	for (GMuint32 i = 0; i < 3; i++)
	{
		d->lightVols.lightVolOrigin[i] = d->lightVols.lightVolSize[i] * ceil(wMins[i] / d->lightVols.lightVolSize[i]);
		maxs[i] = d->lightVols.lightVolSize[i] * floor(wMaxs[i] / d->lightVols.lightVolSize[i]);
		d->lightVols.lightVolBounds[i] = (maxs[i] - d->lightVols.lightVolOrigin[i]) / d->lightVols.lightVolSize[i] + 1;
	}
	numGridPoints = d->lightVols.lightVolBounds[0] * d->lightVols.lightVolBounds[1] * d->lightVols.lightVolBounds[2];

	if (d->header->lumps[LUMP_LIGHTGRID].filelen != numGridPoints * 8)
	{
		gm_warning(gm_dbg_wrap("light volumes mismatch."));
		d->lightBytes.clear();
		return;
	}
}

void GMBSP::parseFromMemory(char *buffer, int size)
{
	D(d);
	d->script = d->scriptstack;
	d->script++;
	if (d->script == &d->scriptstack[MAX_INCLUDES])
		gm_error(gm_dbg_wrap("script file exceeded MAX_INCLUDES"));
	GMString::stringCopy(d->script->filename, "memory buffer");

	d->script->buffer = buffer;
	d->script->line = 1;
	d->script->script_p = d->script->buffer;
	d->script->end_p = d->script->buffer + size;

	d->endofscript = false;
	d->tokenready = false;
}

bool GMBSP::parseEntity(OUT GMBSPEntity** entity)
{
	D(d);

	if (!getToken(true))
		return false;

	if (!GMString::stringEquals(d->token, "{")) {
		gm_warning(gm_dbg_wrap("parseEntity: { not found"));
	}

	GMBSPEntity* result = new GMBSPEntity();
	GMBSPEPair* e;
	do
	{
		if (!getToken(true)) {
			gm_warning(gm_dbg_wrap("parseEntity: EOF without closing brace"));
		}
		if (GMString::stringEquals(d->token, "}")) {
			break;
		}
		e = parseEpair();
		e->next = result->epairs;
		result->epairs = e;

		if (e->key == "origin")
		{
			std::string origin = e->value.toStdString();
			GMScanner s(origin.c_str());
			s.nextFloat(result->origin[0]); // x
			s.nextFloat(result->origin[1]); // z
			s.nextFloat(result->origin[2]); // y
			GM_SWAP(result->origin[1], result->origin[2]);
			result->origin[2] = -result->origin[2];
		}
	} while (true);

	*entity = result;
	return true;
}

bool GMBSP::getToken(bool crossline)
{
	D(d);
	char *token_p;

	if (d->tokenready)                         // is a token allready waiting?
	{
		d->tokenready = false;
		return true;
	}

	if (d->script->script_p >= d->script->end_p)
		return endOfScript(crossline);

	//
	// skip space
	//
skipspace:
	while (*d->script->script_p <= 32)
	{
		if (d->script->script_p >= d->script->end_p)
			return endOfScript(crossline);
		if (*d->script->script_p++ == '\n')
		{
			if (!crossline)
				gm_error(gm_dbg_wrap("Line {0} is incomplete\n"), GMString(d->scriptline) );
			d->scriptline = d->script->line++;
		}
	}

	if (d->script->script_p >= d->script->end_p)
		return endOfScript(crossline);

	// ; # // comments
	if (*d->script->script_p == ';' || *d->script->script_p == '#'
		|| (d->script->script_p[0] == '/' && d->script->script_p[1] == '/'))
	{
		if (!crossline)
			gm_error(gm_dbg_wrap("Line {0} is incomplete"), GMString(d->scriptline) );
		while (*d->script->script_p++ != '\n')
			if (d->script->script_p >= d->script->end_p)
				return endOfScript(crossline);
		d->scriptline = d->script->line++;
		goto skipspace;
	}

	// /* */ comments
	if (d->script->script_p[0] == '/' && d->script->script_p[1] == '*')
	{
		if (!crossline)
		{
			gm_error(gm_dbg_wrap("Line {0} is incomplete"), GMString(d->scriptline) );
			GM_ASSERT(false);
		}
		d->script->script_p += 2;
		while (d->script->script_p[0] != '*' && d->script->script_p[1] != '/')
		{
			if (*d->script->script_p == '\n') {
				d->scriptline = d->script->line++;
			}
			d->script->script_p++;
			if (d->script->script_p >= d->script->end_p)
				return endOfScript(crossline);
		}
		d->script->script_p += 2;
		goto skipspace;
	}

	//
	// copy token
	//
	token_p = d->token;

	if (*d->script->script_p == '"')
	{
		// quoted token
		d->script->script_p++;
		while (*d->script->script_p != '"')
		{
			*token_p++ = *d->script->script_p++;
			if (d->script->script_p == d->script->end_p)
				break;
			if (token_p == &d->token[MAXTOKEN])
				gm_error(gm_dbg_wrap("Token too large."));
		}
		d->script->script_p++;
	}
	else	// regular token
	{
		while (*d->script->script_p > 32 && *d->script->script_p != ';')
		{
			*token_p++ = *d->script->script_p++;
			if (d->script->script_p == d->script->end_p)
				break;
			if (token_p == &d->token[MAXTOKEN])
				gm_error(gm_dbg_wrap("Token too large on line {0}"), GMString(d->scriptline)) ;
		}
	}

	*token_p = 0;

	if (!strcmp(d->token, "$include"))
	{
		getToken(false);
		addScriptToStack(d->token);
		return getToken(crossline);
	}

	return true;
}

GMBSPEPair* GMBSP::parseEpair()
{
	D(d);
	GMBSPEPair *e = new GMBSPEPair();

	if (strlen(d->token) >= MAX_KEY - 1)
		gm_error ("ParseEpar: token too long");

	e->key = d->token;
	getToken(false);
	if (strlen(d->token) >= MAX_VALUE - 1)
		gm_error(gm_dbg_wrap("ParseEpar: token too long"));
	e->value = d->token;

	// strip trailing spaces that sometimes get accidentally
	// added in the editor
	stripTrailing(e->key);

	return e;
}

void GMBSP::addScriptToStack(const char *filename)
{
	D(d);
	GMint32 size;

	d->script++;
	if (d->script == &d->scriptstack[MAX_INCLUDES])
		gm_error(gm_dbg_wrap("script file exceeded MAX_INCLUDES"));
	GMString::stringCopy(d->script->filename, expandPath(filename).toStdString().c_str());

	size = loadFile(d->script->filename, (void **)&d->script->buffer);

	gm_info(gm_dbg_wrap("entering {0}\n"), d->script->filename);

	d->script->line = 1;

	d->script->script_p = d->script->buffer;
	d->script->end_p = d->script->buffer + size;
}


bool GMBSP::endOfScript(bool crossline)
{
	D(d);
	if (!crossline)
	{
		gm_warning(gm_dbg_wrap("Line {0} is incomplete"), GMString(d->scriptline));
	}

	if (!strcmp(d->script->filename, "memory buffer"))
	{
		d->endofscript = true;
		return false;
	}

	free(d->script->buffer);
	if (d->script == d->scriptstack + 1)
	{
		d->endofscript = true;
		return false;
	}
	d->script--;
	d->scriptline = d->script->line;
	gm_info(gm_dbg_wrap("returning to {0}"), d->script->filename );
	return getToken(crossline);
}

END_NS