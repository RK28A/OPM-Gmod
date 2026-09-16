#pragma once

#include <d3dx9.h>
#include <d3d9.h>
#include <d3dtypes.h> // D3DTLVERTEX, used by DrawFilledRect
#include "../../tier0/Vector.h"
#include "../Utils.h"

// <dcommon.h> was included for a D2D1_RECT_F that DrawRect computed and never
// used -- a leftover from a Direct2D implementation.  Both are gone.

#pragma comment(lib, "d3d9.lib")
#pragma comment(lib, "d3dx9.lib")

// D3DX device objects.
//
// These are D3DPOOL_DEFAULT resources: a device reset -- alt-tab, a resolution
// change, a mode switch -- invalidates them, and using one afterwards is
// undefined.  Nothing here used to cope with that, which is the "runtime QA"
// item REVIEW.md carried as untested; it was not merely untested, it was not
// implemented.  OnLostDevice()/OnResetDevice() below are the two halves that
// have to bracket an IDirect3DDevice9::Reset, and ShutdownRenderer() is what
// the Unload button needs so the objects are released rather than leaked.
ID3DXLine* pLine = nullptr;
ID3DXFont* pFont = nullptr;
IDirect3DDevice9* pDevice = nullptr;

#ifdef _DEBUG
ID3DXFont* pDebugFont = nullptr;
#endif

int DrawingFontSize = 11;

// True between a successful InitRenderer() and the next device loss.  Every
// draw helper below checks it, so a failed creation or a lost device degrades
// to "nothing is drawn" instead of dereferencing null.
bool gRendererReady = false;

void InitRenderer(IDirect3DDevice9* pdevice)
{
	pDevice = pdevice;
	gRendererReady = false;

	if (!pdevice)
		return;

	// Both creations used to have their HRESULT discarded, so a failure left
	// pLine/pFont null and every later pLine->Begin() was a null dereference.
	if (FAILED(D3DXCreateLine(pdevice, &pLine)))
		pLine = nullptr;

	if (FAILED(D3DXCreateFont(pdevice, DrawingFontSize, 0, FW_HEAVY, 1, FALSE, DEFAULT_CHARSET,
		OUT_DEFAULT_PRECIS, ANTIALIASED_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Tahoma", &pFont)))
		pFont = nullptr;

#ifdef _DEBUG
	if (FAILED(D3DXCreateFont(pdevice, 15, 0, FW_HEAVY, 1, FALSE, DEFAULT_CHARSET,
		OUT_DEFAULT_PRECIS, ANTIALIASED_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Arial", &pDebugFont)))
		pDebugFont = nullptr;
#endif

	gRendererReady = (pLine != nullptr && pFont != nullptr);

	if (!gRendererReady)
		ConPrint("D3DX renderer init failed; overlays are disabled this session", Color(255, 200, 0));
}

// Call immediately *before* IDirect3DDevice9::Reset.
void OnLostDevice()
{
	gRendererReady = false;

	if (pLine)
		pLine->OnLostDevice();
	if (pFont)
		pFont->OnLostDevice();
#ifdef _DEBUG
	if (pDebugFont)
		pDebugFont->OnLostDevice();
#endif
}

// Call immediately *after* a successful IDirect3DDevice9::Reset.
void OnResetDevice()
{
	bool ok = true;

	if (pLine)
		ok = SUCCEEDED(pLine->OnResetDevice()) && ok;
	if (pFont)
		ok = SUCCEEDED(pFont->OnResetDevice()) && ok;
#ifdef _DEBUG
	if (pDebugFont)
		pDebugFont->OnResetDevice();
#endif

	gRendererReady = ok && pLine != nullptr && pFont != nullptr;
}

// Releases everything.  Used by the Unload button, which used to leave all of
// this mapped and referenced by the device.
void ShutdownRenderer()
{
	gRendererReady = false;

	if (pLine)
	{
		pLine->Release();
		pLine = nullptr;
	}
	if (pFont)
	{
		pFont->Release();
		pFont = nullptr;
	}
#ifdef _DEBUG
	if (pDebugFont)
	{
		pDebugFont->Release();
		pDebugFont = nullptr;
	}
#endif

	pDevice = nullptr;
}

// Renamed from _DrawTextW / DrawTextW.
//
// `DrawTextW` is a Win32 API function (winuser.h).  Defining a function-like
// macro with that name shadows it for every translation unit that sees this
// header, and because macros expand regardless of what precedes them, even
// `font->DrawTextW(...)` would have been rewritten -- it only worked because
// the definition happened to come after the one member call that uses it.
void DrawStringImpl(Vector pos, const std::wstring& txt, ULONG color, bool outlined, ID3DXFont* font)
{
	if (!font)
		return;

	const auto text = [&](const std::wstring& value, int x, int y, unsigned long colour) {
		RECT r{ x, y, x, y };
		font->DrawTextW(nullptr, value.c_str(), -1, &r, DT_NOCLIP, colour);
	};

	const int x = static_cast<int>(pos.x);
	const int y = static_cast<int>(pos.y);

	if (outlined) {
		const unsigned long outlineColour = D3DCOLOR_RGBA(1, 1, 1, 255);
		text(txt, x - 1, y, outlineColour);
		text(txt, x + 1, y, outlineColour);
		text(txt, x, y - 1, outlineColour);
		text(txt, x, y + 1, outlineColour);
	}

	text(txt, x, y, color);
}

#define DebugDrawString(a,b,c,d) DrawStringImpl(a,b,c,d,pDebugFont)
#define DrawString(a,b,c,d) DrawStringImpl(a,b,c,d,pFont)

void DrawLineOutlined(Vector from, Vector to, ULONG color) {
	if (!gRendererReady)
		return;

	D3DXVECTOR2 lines[2] = {
		D3DXVECTOR2(from.x, from.y),
		D3DXVECTOR2(to.x, to.y)
	};
	D3DXVECTOR2 outline1[2] = {
		D3DXVECTOR2(from.x - 1, from.y - 1),
		D3DXVECTOR2(to.x - 1, to.y - 1)
	};
	D3DXVECTOR2 outline2[2] = {
		D3DXVECTOR2(from.x + 1, from.y + 1),
		D3DXVECTOR2(to.x + 1, to.y + 1)
	};

	pLine->Begin();
	pLine->Draw(lines, 2, color);
	pLine->Draw(outline1, 2, 0xFF000000);
	pLine->Draw(outline2, 2, 0xFF000000);
	pLine->End();
}

void DrawLine(Vector from, Vector to, ULONG color) {
	if (!gRendererReady)
		return;

	D3DXVECTOR2 lines[2] = {
		D3DXVECTOR2(from.x, from.y),
		D3DXVECTOR2(to.x, to.y)
	};

	pLine->Begin();
	pLine->Draw(lines, 2, color);
	pLine->End();
}

// Segment count for the circle, not the radius.
//
// The only caller passed Settings::Aimbot::aimbotFOV for *both* the radius and
// the vertex count, so the smoothness tracked the size by accident and a FOV of
// zero divided 2*pi by zero.  The count is bounded here and the caller passes a
// constant.
inline constexpr int kCircleMinSegments = 8;
inline constexpr int kCircleMaxSegments = 60;

void DrawCircle(Vector pos, float radius, int segments, ULONG color) {
	if (!gRendererReady)
		return;

	if (!std::isfinite(radius) || radius <= 0.f)
		return;

	if (segments < kCircleMinSegments)
		segments = kCircleMinSegments;
	else if (segments > kCircleMaxSegments)
		segments = kCircleMaxSegments;

	// segments + 1 points closes the loop; the buffer is sized from the bound
	// above rather than from a magic 128.
	D3DXVECTOR2 points[kCircleMaxSegments + 1];

	const float step = (D3DX_PI * 2.f) / static_cast<float>(segments);
	for (int i = 0; i <= segments; ++i)
	{
		const float angle = step * static_cast<float>(i);
		points[i].x = radius * std::cos(angle) + pos.x;
		points[i].y = radius * std::sin(angle) + pos.y;
	}

	// Begin()/End() were missing here, unlike every other helper in this file.
	pLine->Begin();
	pLine->Draw(points, segments + 1, color);
	pLine->End();
}

void DrawFilledRect(Vector pos, float height, float width, ULONG color) {
	if (!gRendererReady || !pDevice)
		return;

	D3DTLVERTEX qV[4] = {
		{ pos.x, pos.y + height, 0.f, 1.f, color },
		{ pos.x, pos.y, 0.f, 1.f, color },
		{ pos.x + width, pos.y + height, 0.f, 1.f, color },
		{ pos.x + width, pos.y , 0.f, 1.f, color }
	};

	pDevice->SetRenderState(D3DRS_ALPHABLENDENABLE, true);
	pDevice->SetRenderState(D3DRS_DESTBLEND, D3DBLEND_INVSRCALPHA);
	pDevice->SetFVF(D3DFVF_XYZRHW | D3DFVF_DIFFUSE | D3DFVF_TEX1);
	pDevice->SetTexture(0, nullptr);
	pDevice->DrawPrimitiveUP(D3DPT_TRIANGLESTRIP, 2, qV, sizeof(D3DTLVERTEX));
}

void DrawEsp2D(Vector targetPos, Vector targetTop, ULONG color) {

	float height = targetTop.y - targetPos.y;
	Vector tl, tr;
	tl.x = targetTop.x - height / 4 ;
	tr.x = targetTop.x + height / 4;
	tl.y = tr.y = targetTop.y;

	Vector bl, br;
	bl.x = targetPos.x - height / 4;
	br.x = targetPos.x + height / 4;
	bl.y = br.y = targetPos.y;

	DrawLineOutlined(tl, tr, color);
	DrawLineOutlined(bl, br, color);
	DrawLineOutlined(tl, bl, color);
	DrawLineOutlined(tr, br, color);
}

// Yaw only.  Pitch and roll are not applied, so the 3D box is wrong for any
// entity that is not upright -- a ragdoll, a vehicle, a prop on its side.
// Kept as-is because fixing it is a behaviour change to the visuals rather than
// a defect fix, but the limitation is now stated rather than left as a "Help
// welcome" note.
void RotateVec(QAngle ang, Vector& in)
{
	const float sinYaw = std::sin(DEG2RAD(ang.y));
	const float cosYaw = std::cos(DEG2RAD(ang.y));

	const Vector original(in.x, in.y, in.z);

	in.x = (original.x * cosYaw) - (original.y * sinYaw);
	in.y = (original.x * sinYaw) + (original.y * cosYaw);
}

void DrawEspBox3D(Vector max, Vector min, Vector orig, QAngle ang, D3DCOLOR color)
{
	const float height3D = max.z - min.z;

	Vector b1(min.x, min.y, min.z);
	Vector b2(min.x, max.y, min.z);
	Vector b3(max.x, max.y, min.z);
	Vector b4(max.x, min.y, min.z);

	RotateVec(ang, b1);
	RotateVec(ang, b2);
	RotateVec(ang, b3);
	RotateVec(ang, b4);

	b1 += orig;
	b2 += orig;
	b3 += orig;
	b4 += orig;

	Vector t1 = Vector(b1.x, b1.y, b1.z + height3D);
	Vector t2 = Vector(b2.x, b2.y, b2.z + height3D);
	Vector t3 = Vector(b3.x, b3.y, b3.z + height3D);
	Vector t4 = Vector(b4.x, b4.y, b4.z + height3D);

	Vector b1_2, b2_2, b3_2, b4_2, t1_2, t2_2, t3_2, t4_2;

	if (WorldToScreen(b1, b1_2) && WorldToScreen(b2, b2_2) && WorldToScreen(b3, b3_2) && WorldToScreen(b4, b4_2)
		&& WorldToScreen(t1, t1_2) && WorldToScreen(t2, t2_2) && WorldToScreen(t3, t3_2) && WorldToScreen(t4, t4_2))
	{
		// columns
		DrawLine(t1_2, b1_2, color);
		DrawLine(t2_2, b2_2, color);
		DrawLine(t3_2, b3_2, color);
		DrawLine(t4_2, b4_2, color);
		// top face
		DrawLine(t1_2, t2_2, color);
		DrawLine(t2_2, t3_2, color);
		DrawLine(t3_2, t4_2, color);
		DrawLine(t4_2, t1_2, color);
		// bottom face
		DrawLine(b1_2, b2_2, color);
		DrawLine(b2_2, b3_2, color);
		DrawLine(b3_2, b4_2, color);
		DrawLine(b4_2, b1_2, color);
	}
}
