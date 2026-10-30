#pragma once

#include <list>
#include <string>
#include <vector>
#include <jni.h>
#include "KeyboardCore.h"

// ============================================================================
//  Teclado NATIVO do chat (ImGui). Nao depende do teclado do Android, de fonte de icone nem de
//  View Java: campo de texto com cursor, botoes de historico (cima/baixo) e "/", layout pt-BR
//  (c-cedilha e acentos), Shift/Caps, simbolos. A logica esta em KeyboardCore.h (testada no PC).
// ============================================================================
class Keyboard : public Widget
{
public:
	Keyboard();

	void show(Widget* caller);
	void hide();
	void send();
	void sendForGB(JNIEnv *pEnv, jobject thiz, jbyteArray str);   // legado (JNI onInputEnd)

	virtual void performLayout() override;
	virtual void draw(ImGuiRenderer* renderer) override;
	virtual void touchEvent(const ImVec2& pos, TouchType type) override;
	virtual void activateEvent(bool active) override;

private:
	enum class Target { None, Key, Older, Newer, Slash, Field };

	struct Hit
	{
		Target target = Target::None;
		int row = -1, col = -1;
		bool operator==(const Hit& o) const { return target == o.target && row == o.row && col == o.col; }
	};

	void   close(bool saveDraft);
	void   applyGeometry();
	void   ensureKeyRects();
	Hit    hitTest(const ImVec2& pos) const;
	void   act(const Hit& h);
	void   onAction(KbCore::Action a);
	long   nowMs() const;

	// geometria (coordenadas ABSOLUTAS de tela)
	float  inputH() const;
	float  rowH() const;
	void   inputRects(ImVec2& fieldA, ImVec2& fieldB, ImVec2 btnA[3], ImVec2 btnB[3]) const;

	void   drawKey(ImGuiRenderer* r, const KbCore::KeyDef& k, const ImVec2& a, const ImVec2& b, bool pressed, float fs) const;

	KbCore::Machine m_core;
	Widget*     m_caller;
	Widget*     m_draftOwner;
	std::string m_draft;

	// teclas da pagina desenhada
	KbCore::Page m_cachedPage;
	float        m_cachedW, m_cachedH;
	std::vector<KbCore::Row>                 m_rows;
	std::vector<std::vector<KbCore::Rect>>   m_rects;   // relativas ao canto da area de teclas

	// toque
	Hit   m_pressed;
	long  m_pressT;
	long  m_lastRepeatT;
	bool  m_backspaceFired;
	bool  m_wantCaretFromTap;
	float m_tapX;

	// campo
	float m_scroll;
	long  m_blinkBase;
};
