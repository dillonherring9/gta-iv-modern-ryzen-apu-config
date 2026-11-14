#include "vcr_client.h"

namespace vcr {

#define FOURCC(a, b, c, d) ((DWORD) (BYTE) (a) | ((DWORD) (BYTE) (b) << 8) | ((DWORD) (BYTE) (c) << 16) | ((DWORD) (BYTE) (d) << 24))

FmtInfo fmtInfo(D3DFORMAT f) {
  switch ((DWORD) f) {
  case D3DFMT_DXT1: return {4, 4, 8, true};
  case D3DFMT_DXT2: case D3DFMT_DXT3: case D3DFMT_DXT4: case D3DFMT_DXT5: return {4, 4, 16, true};
  case FOURCC('A', 'T', 'I', '1'): return {4, 4, 8, true};
  case FOURCC('A', 'T', 'I', '2'): return {4, 4, 16, true};
  case D3DFMT_UYVY: case D3DFMT_YUY2: case D3DFMT_R8G8_B8G8: case D3DFMT_G8R8_G8B8: return {2, 1, 4, true};
  case D3DFMT_R8G8B8: return {1, 1, 3, true};
  case D3DFMT_A8R8G8B8: case D3DFMT_X8R8G8B8: case D3DFMT_A8B8G8R8: case D3DFMT_X8B8G8R8: case D3DFMT_A2R10G10B10:
  case D3DFMT_A2B10G10R10: case D3DFMT_G16R16: case D3DFMT_Q8W8V8U8: case D3DFMT_V16U16: case D3DFMT_X8L8V8U8:
  case D3DFMT_A2W10V10U10: case D3DFMT_R32F: case D3DFMT_G16R16F: case D3DFMT_D32: case D3DFMT_D24S8: case D3DFMT_D24X8:
  case D3DFMT_D24X4S4: case D3DFMT_D32F_LOCKABLE: case D3DFMT_D24FS8: case D3DFMT_INDEX32: case D3DFMT_D32_LOCKABLE:
    return {1, 1, 4, true};
  case D3DFMT_R5G6B5: case D3DFMT_X1R5G5B5: case D3DFMT_A1R5G5B5: case D3DFMT_A4R4G4B4: case D3DFMT_A8R3G3B2:
  case D3DFMT_X4R4G4B4: case D3DFMT_A8L8: case D3DFMT_V8U8: case D3DFMT_L6V5U5: case D3DFMT_L16: case D3DFMT_R16F:
  case D3DFMT_D16: case D3DFMT_D16_LOCKABLE: case D3DFMT_D15S1: case D3DFMT_INDEX16: case D3DFMT_A8P8: case D3DFMT_CxV8U8:
    return {1, 1, 2, true};
  case D3DFMT_A8: case D3DFMT_L8: case D3DFMT_P8: case D3DFMT_R3G3B2: case D3DFMT_A4L4: case D3DFMT_S8_LOCKABLE:
    return {1, 1, 1, true};
  case D3DFMT_A16B16G16R16: case D3DFMT_Q16W16V16U16: case D3DFMT_A16B16G16R16F: case D3DFMT_G32R32F: return {1, 1, 8, true};
  case D3DFMT_A32B32G32R32F: return {1, 1, 16, true};
  case D3DFMT_A1: return {32, 1, 4, true};
  default: return {1, 1, 4, false};   // vendor FOURCCs (INTZ, DF24, NULL ...) are not lockable; size is nominal
  }
}
uint32_t rowPitch(D3DFORMAT f, uint32_t width) {
  FmtInfo i = fmtInfo(f);
  uint32_t row = ((width + i.bw - 1) / i.bw) * i.bytes;
  return (row + 3) & ~3u;
}
uint32_t blockRows(D3DFORMAT f, uint32_t height) { FmtInfo i = fmtInfo(f); return (height + i.bh - 1) / i.bh; }
uint32_t mipDim(uint32_t d, uint32_t level) { uint32_t v = d >> level; return v ? v : 1; }
uint32_t mipLevels(uint32_t w, uint32_t h, uint32_t d) {
  uint32_t m = w > h ? w : h; if (d > m) m = d;
  uint32_t n = 1; while (m > 1) { m >>= 1; n++; } return n;
}
bool isDepthFormat(D3DFORMAT f) {
  switch ((DWORD) f) {
  case D3DFMT_D16_LOCKABLE: case D3DFMT_D32: case D3DFMT_D15S1: case D3DFMT_D24S8: case D3DFMT_D24X8: case D3DFMT_D24X4S4:
  case D3DFMT_D16: case D3DFMT_D32F_LOCKABLE: case D3DFMT_D24FS8: case D3DFMT_D32_LOCKABLE: case D3DFMT_S8_LOCKABLE:
  case FOURCC('I', 'N', 'T', 'Z'): case FOURCC('D', 'F', '2', '4'): case FOURCC('D', 'F', '1', '6'):
    return true;
  default: return false;
  }
}
bool isLockableDepth(D3DFORMAT f) {
  return f == D3DFMT_D16_LOCKABLE || f == D3DFMT_D32F_LOCKABLE || f == D3DFMT_D32_LOCKABLE || f == D3DFMT_S8_LOCKABLE;
}

// shader bytecode length in bytes, including the end token
uint32_t shaderBytes(const DWORD* code) {
  if (!code) return 0;
  const DWORD* p = code;
  DWORD version = *p++;
  uint32_t major = (version >> 8) & 0xFF;
  for (uint32_t guard = 0; guard < (1u << 22); guard++) {
    DWORD tok = *p;
    if (tok == 0x0000FFFF) return (uint32_t) ((p - code + 1) * 4);
    if ((tok & 0xFFFF) == 0xFFFE) { p += 1 + ((tok >> 16) & 0x7FFF); continue; }   // comment
    if (major >= 2) { p += 1 + ((tok >> 24) & 0x0F); continue; }                     // instruction length field
    // shader model 1: step over parameter tokens (bit 31 set)
    p++;
    while ((*p & 0x80000000u) && *p != 0x0000FFFF) p++;
  }
  return 0;
}

uint32_t fvfStride(DWORD fvf) {
  uint32_t s = 0;
  switch (fvf & D3DFVF_POSITION_MASK) {
  case D3DFVF_XYZ: s += 12; break; case D3DFVF_XYZRHW: s += 16; break; case D3DFVF_XYZW: s += 16; break;
  case D3DFVF_XYZB1: s += 16; break; case D3DFVF_XYZB2: s += 20; break; case D3DFVF_XYZB3: s += 24; break;
  case D3DFVF_XYZB4: s += 28; break; case D3DFVF_XYZB5: s += 32; break;
  }
  if (fvf & D3DFVF_NORMAL) s += 12;
  if (fvf & D3DFVF_PSIZE) s += 4;
  if (fvf & D3DFVF_DIFFUSE) s += 4;
  if (fvf & D3DFVF_SPECULAR) s += 4;
  uint32_t n = (fvf & D3DFVF_TEXCOUNT_MASK) >> D3DFVF_TEXCOUNT_SHIFT;
  for (uint32_t i = 0; i < n; i++) {
    switch ((fvf >> (16 + i * 2)) & 3) { case D3DFVF_TEXTUREFORMAT1: s += 4; break; case D3DFVF_TEXTUREFORMAT2: s += 8; break;
                                         case D3DFVF_TEXTUREFORMAT3: s += 12; break; case D3DFVF_TEXTUREFORMAT4: s += 16; break; }
  }
  return s;
}

std::vector<D3DVERTEXELEMENT9> fvfToDecl(DWORD fvf) {
  std::vector<D3DVERTEXELEMENT9> e; WORD off = 0;
  auto add = [&](BYTE type, BYTE usage, BYTE idx, WORD size) { e.push_back({0, off, type, D3DDECLMETHOD_DEFAULT, usage, idx}); off += size; };
  DWORD pos = fvf & D3DFVF_POSITION_MASK;
  int blend = 0;
  switch (pos) {
  case D3DFVF_XYZ: add(D3DDECLTYPE_FLOAT3, D3DDECLUSAGE_POSITION, 0, 12); break;
  case D3DFVF_XYZRHW: add(D3DDECLTYPE_FLOAT4, D3DDECLUSAGE_POSITIONT, 0, 16); break;
  case D3DFVF_XYZW: add(D3DDECLTYPE_FLOAT4, D3DDECLUSAGE_POSITION, 0, 16); break;
  case D3DFVF_XYZB1: blend = 1; break; case D3DFVF_XYZB2: blend = 2; break; case D3DFVF_XYZB3: blend = 3; break;
  case D3DFVF_XYZB4: blend = 4; break; case D3DFVF_XYZB5: blend = 5; break;
  }
  if (blend) {
    add(D3DDECLTYPE_FLOAT3, D3DDECLUSAGE_POSITION, 0, 12);
    bool lastUbyte = (fvf & D3DFVF_LASTBETA_UBYTE4) || (fvf & D3DFVF_LASTBETA_D3DCOLOR);
    int weights = lastUbyte ? blend - 1 : blend;
    if (weights > 0) add((BYTE) (D3DDECLTYPE_FLOAT1 + weights - 1), D3DDECLUSAGE_BLENDWEIGHT, 0, (WORD) (4 * weights));
    if (lastUbyte) add((fvf & D3DFVF_LASTBETA_UBYTE4) ? D3DDECLTYPE_UBYTE4 : D3DDECLTYPE_D3DCOLOR, D3DDECLUSAGE_BLENDINDICES, 0, 4);
  }
  if (fvf & D3DFVF_NORMAL) add(D3DDECLTYPE_FLOAT3, D3DDECLUSAGE_NORMAL, 0, 12);
  if (fvf & D3DFVF_PSIZE) add(D3DDECLTYPE_FLOAT1, D3DDECLUSAGE_PSIZE, 0, 4);
  if (fvf & D3DFVF_DIFFUSE) add(D3DDECLTYPE_D3DCOLOR, D3DDECLUSAGE_COLOR, 0, 4);
  if (fvf & D3DFVF_SPECULAR) add(D3DDECLTYPE_D3DCOLOR, D3DDECLUSAGE_COLOR, 1, 4);
  uint32_t n = (fvf & D3DFVF_TEXCOUNT_MASK) >> D3DFVF_TEXCOUNT_SHIFT;
  for (uint32_t i = 0; i < n; i++) {
    switch ((fvf >> (16 + i * 2)) & 3) {
    case D3DFVF_TEXTUREFORMAT1: add(D3DDECLTYPE_FLOAT1, D3DDECLUSAGE_TEXCOORD, (BYTE) i, 4); break;
    case D3DFVF_TEXTUREFORMAT2: add(D3DDECLTYPE_FLOAT2, D3DDECLUSAGE_TEXCOORD, (BYTE) i, 8); break;
    case D3DFVF_TEXTUREFORMAT3: add(D3DDECLTYPE_FLOAT3, D3DDECLUSAGE_TEXCOORD, (BYTE) i, 12); break;
    case D3DFVF_TEXTUREFORMAT4: add(D3DDECLTYPE_FLOAT4, D3DDECLUSAGE_TEXCOORD, (BYTE) i, 16); break;
    }
  }
  e.push_back(D3DVERTEXELEMENT9 D3DDECL_END());
  return e;
}

// best-effort reverse mapping used for GetFVF after SetVertexDeclaration (0 when not expressible)
DWORD declToFvf(const std::vector<D3DVERTEXELEMENT9>& el) {
  DWORD fvf = 0; int tex = 0;
  for (const auto& e : el) {
    if (e.Stream == 0xFF) break;
    if (e.Stream != 0) return 0;
    switch (e.Usage) {
    case D3DDECLUSAGE_POSITION: if (e.UsageIndex == 0) { if (e.Type == D3DDECLTYPE_FLOAT3) fvf |= D3DFVF_XYZ; else if (e.Type == D3DDECLTYPE_FLOAT4) fvf |= D3DFVF_XYZW; else return 0; } break;
    case D3DDECLUSAGE_POSITIONT: fvf |= D3DFVF_XYZRHW; break;
    case D3DDECLUSAGE_NORMAL: fvf |= D3DFVF_NORMAL; break;
    case D3DDECLUSAGE_PSIZE: fvf |= D3DFVF_PSIZE; break;
    case D3DDECLUSAGE_COLOR: fvf |= e.UsageIndex == 0 ? D3DFVF_DIFFUSE : D3DFVF_SPECULAR; break;
    case D3DDECLUSAGE_TEXCOORD: {
      DWORD f = e.Type == D3DDECLTYPE_FLOAT1 ? D3DFVF_TEXTUREFORMAT1 : e.Type == D3DDECLTYPE_FLOAT2 ? D3DFVF_TEXTUREFORMAT2
              : e.Type == D3DDECLTYPE_FLOAT3 ? D3DFVF_TEXTUREFORMAT3 : D3DFVF_TEXTUREFORMAT4;
      fvf |= f << (16 + e.UsageIndex * 2); if (e.UsageIndex + 1 > tex) tex = e.UsageIndex + 1; break; }
    default: return 0;
    }
  }
  return fvf | ((DWORD) tex << D3DFVF_TEXCOUNT_SHIFT);
}

uint32_t vertexCount(D3DPRIMITIVETYPE t, uint32_t n) {
  switch (t) {
  case D3DPT_POINTLIST: return n;
  case D3DPT_LINELIST: return n * 2;
  case D3DPT_LINESTRIP: return n + 1;
  case D3DPT_TRIANGLELIST: return n * 3;
  case D3DPT_TRIANGLESTRIP: case D3DPT_TRIANGLEFAN: return n + 2;
  default: return 0;
  }
}

}  // namespace vcr
