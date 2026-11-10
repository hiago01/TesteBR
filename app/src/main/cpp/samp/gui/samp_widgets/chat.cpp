#include "../../game/Diag.h"
#include "../gui.h"
#include "../../main.h"
#include "../../game/game.h"
#include "../../net/netgame.h"
#include <algorithm>
#include <atomic>
#include "../settings.h"
#include "java/jniutil.h"
#include "../../game/sprite2d.h"
#include "../../game/ClothesTest.h"
#include "../../game/ClothesSync.h"
#include "../../game/Hose.h"
#include "../../game/Trena.h"
#include "../../game/IdCard3D.h"
#include "../../game/Giroflex.h"
#include "../../game/CustomSkin.h"
#include "../../game/CorpTex.h"
#include "../../game/RadarSkin.h"
#include <cstring>
#include <cctype>
#include <string>
#include "../../game/camerazoom.h"
#include "../../game/CustomAnimTest.h"
#include "../../game/CustomDiag.h"
#include "../../game/movemode.h"
#include "controlmode.h"
#include "controlsselector.h"
extern UI* pUI;
extern CGame* pGame;
extern CNetGame* pNetGame;
extern CSettings* pSettings;
extern CJavaWrapper *pJavaWrapper;

namespace { std::atomic<bool> g_chatAllowed{false}; }
void ChatSetAllowed(bool allowed) { g_chatAllowed.store(allowed); }
static std::atomic<bool> g_provAllowed{false};
void ProvadorSetAllowed(bool allowed) { g_provAllowed.store(allowed); }
bool ProvadorAllowed() { return g_provAllowed.load(); }
bool ChatAllowed() { return g_chatAllowed.load(); }

Chat::Chat()
	: ListBox()
{

}

void Chat::addChatMessage(const std::string& message, const std::string& nick, const ImColor& nick_color)
{
	addPlayerMessage(message, nick, nick_color);
}

void Chat::addInfoMessage(const std::string& format, ...)
{
	char tmp_buf[512];

	va_list args;
	va_start(args, format);
	vsprintf(tmp_buf, format.c_str(), args);
	va_end(args);

	addMessage(std::string(tmp_buf), ImColor(0x00, 0xc8, 0xc8));
}

void Chat::addDebugMessage(const std::string& format, ...)
{
	char tmp_buf[512];

	va_list args;
	va_start(args, format);
	vsprintf(tmp_buf, format.c_str(), args);
	va_end(args);

	addMessage(std::string(tmp_buf), ImColor(0xbe, 0xbe, 0xbe));
}

void Chat::addClientMessage(const std::string& message, const ImColor& color)
{
	addMessage(message, color);
}

void Chat::addMessage(const std::string& message, const ImColor& color)
{
	if (this->itemsCount() > UISettings::chatMaxMessages())
	{
		this->removeItem(0);
	}

	MessageItem* item = new MessageItem(message, color);
	this->addItem(item);
	/*if(!active())*/ this->setScrollY(1.0f);
}

void Chat::addPlayerMessage(const std::string& message, const std::string& nick, const ImColor& nick_color)
{
	if (this->itemsCount() > UISettings::chatMaxMessages())
	{
		this->removeItem(0);
	}

	PlayerMessageItem* item = new PlayerMessageItem(message, nick, nick_color);
	this->addItem(item);
	/*if(!active())*/ this->setScrollY(1.0f);
}

void Chat::draw(ImGuiRenderer* renderer)
{
	if (!g_chatAllowed.load()) return;   // jogador comum: sem chat
	ListBox::draw(renderer);
}

void Chat::activateEvent(bool active)
{
	if (active)
	{
		this->setScrollable(true);
	}
	else
	{
		this->setScrollable(false);
	}
}

void Chat::touchPopEvent()
{
	if (!g_chatAllowed.load()) return;   // jogador comum: o toque no chat nao abre o teclado
	if (pUI->playertablist()->visible()) return;

	pUI->keyboard()->show(this);
}

void Chat::keyboardEvent(const std::string& input)
{

if (input.length() > 0 && pNetGame)
{
        if (input == "/testepng")
        {
                bool result = CSprite2d_TestCustomRadarDisc();

                if (result)
                {
                        addDebugMessage(
                                "[TESTE PNG] radardisc.png carregado com sucesso!"
                        );
                }
                else
                {
                        addDebugMessage(
                                "[TESTE PNG] Falha ao carregar radardisc.png"
                        );
                }

                return;
        }
if (input == "/celofs")
        {
            // painel de ajuste do celular na mao (esquerda, acima do joystick). Com parametros o comando segue para o servidor.
            CelOfsPanel::Toggle();
            addDebugMessage(CelOfsPanel::IsOpen() ? "[painel] /celofs aberto (precisa ser admin RCON; /celofs de novo fecha)" : "[painel] /celofs fechado");
            return;
        }
if (input == "/testeclothes")
        {
            TestPlayerClothesDesc();

            addDebugMessage(
                "[CLOTHES TEST] Verifique o samp.log"
            );

            return;
        }
if (input == "/cj" || input.rfind("/cj ", 0) == 0)
        {
            // CJ real: /cj <componente> <textura|-> [modelo] | /cj reset | /cj remote <0|1>
            std::string args = input.size() > 3 ? input.substr(4) : std::string();
            std::string reply = CClothesSync::DebugCommand(args);
            addDebugMessage("%s", reply.c_str());

            return;
        }
if (input == "/skin7" || input.rfind("/skin7 ", 0) == 0)
        {
            // skin DFF customizada: /skin7 load | info | unload
            std::string args = input.size() > 6 ? input.substr(7) : std::string();
            std::string reply = CCustomSkin::DebugCommand(args);
            addDebugMessage("%s", reply.c_str());

            return;
        }
if (input == "/trena" || input.rfind("/trena ", 0) == 0)
        {
            // medidor de distancia: /trena a | /trena | /trena limpar | /trena ir <x> <y> <z>
            const std::string args = input.size() > 6 ? input.substr(7) : std::string();
            const std::string reply = Trena_Command(args);
            size_t p0 = 0;
            while (p0 <= reply.size()) { size_t p1 = reply.find('\n', p0); if (p1 == std::string::npos) p1 = reply.size(); addDebugMessage("%s", reply.substr(p0, p1 - p0).c_str()); p0 = p1 + 1; }
            return;
        }
if (input == "/coord" || input.rfind("/coord ", 0) == 0)
        {
            // salva coordenadas em SAMP/coordenadas.txt: /coord [nome] | lista | apagar
            const std::string args = input.size() > 6 ? input.substr(7) : std::string();
            const std::string reply = Coord_Command(args);
            size_t p0 = 0;
            while (p0 <= reply.size()) { size_t p1 = reply.find('\n', p0); if (p1 == std::string::npos) p1 = reply.size(); addDebugMessage("%s", reply.substr(p0, p1 - p0).c_str()); p0 = p1 + 1; }
            return;
        }
if (input == "/lodinfo")
        {
            const std::string reply = LodInfo_Command("");
            size_t p0 = 0;
            while (p0 <= reply.size()) { size_t p1 = reply.find('\n', p0); if (p1 == std::string::npos) p1 = reply.size(); addDebugMessage("%s", reply.substr(p0, p1 - p0).c_str()); p0 = p1 + 1; }
            return;
        }
if (input == "/diag")
        {
            addDebugMessage("%s", Diag::Summary().c_str());
            return;
        }
if (input == "/radarskin" || input.rfind("/radarskin ", 0) == 0)
        {
            // tiles PNG (Google Maps) no radar nativo: /radarskin on|off|reload|info
            const std::string args = input.size() > 10 ? input.substr(11) : std::string();
            addDebugMessage("%s", RadarSkin::Command(args).c_str());
            return;
        }
if (input == "/mapa" || input.rfind("/mapa ", 0) == 0)
        {
            // mapa aberto: /mapa (abre/fecha) | /mapa grade | /mapa info | /mapa ruas [on|off] | /mapa nodes | /mapa linhas
            const std::string args = input.size() > 5 ? input.substr(6) : std::string();
            if (args == "grade" || args == "info" || args == "nodes" || args == "linhas" || args.rfind("ruas", 0) == 0 || args.rfind("minimapa", 0) == 0) addDebugMessage("%s", WorldMapWidget::DebugCommand(args).c_str());
            else WorldMapWidget::Toggle();
            return;
        }
if (input == "/provador" || input.rfind("/provador ", 0) == 0)
        {
            // interface de personalizacao do /skin7: /provador (abre/fecha) | /provador fechar
            // (/roupas ja existe: tela HTML ligada ao servidor)
            const std::string args = input.size() > 9 ? input.substr(10) : std::string();
            if (!ProvadorAllowed()) { addDebugMessage("O /provador est\xC3\xA1 bloqueado: s\xC3\xB3 administradores ou jogadores liberados."); return; }
            if (args == "fechar") Provador::RequestClose();
            else                  Provador::Toggle();

            return;
        }
const std::string corpArgs = input.size() > 5 ? input.substr(6) : std::string();
        // /corp <nome> e /corp limpar vao para o SERVIDOR (online: grava e todos veem); so info/lista/271/7 sao locais
        if ((input == "/corp" || input.rfind("/corp ", 0) == 0)
            && (corpArgs.empty() || corpArgs == "info" || corpArgs == "lista" || corpArgs == "list" || corpArgs == "271" || corpArgs == "7"))
        {
            // diagnostico local: /corp | info | lista | 271 | 7
            std::string args = corpArgs;
            std::string reply = CorpTex::DebugCommand(args);
            addDebugMessage("%s", reply.c_str());

            return;
        }
        for (const char* vc : {"/rosto", "/cabelo", "/pele", "/visual", "/bota", "/calca", "/camisa", "/intima", "/roupa", "/cop"})
        {
            const size_t n = std::strlen(vc);
            if (input == vc || input.rfind(std::string(vc) + " ", 0) == 0)
            {
                // aparencia no seu ator: /rosto <tom> <variante> | /cabelo <estilo> <cor> | /pele <tom> | /visual [limpar]
                std::string args = input.size() > n ? input.substr(n + 1) : std::string();
                std::string reply = CorpTex::VisualCommand(std::string(vc + 1), args);
                addDebugMessage("%s", reply.c_str());

                return;
            }
        }
if (input == "/mangueira" || input.rfind("/mangueira ", 0) == 0)
        {
            // mangueira de parede: /mangueira a | pegar | soltar | limpar | config ...
            std::string args = input.size() > 10 ? input.substr(11) : std::string();
            std::string reply = Hose_Command(args);
            addDebugMessage("%s", reply.c_str());

            return;
        }
// /tablet (ou /maximoos): abre/fecha o tablet interno (Maximo OS, etapa 1: infraestrutura).
if (input == "/tablet" || input == "/maximoos")
{
        MaximoOSWidget::Toggle();
        return;
}

// /celular (ou /phone): abre/fecha o celular (iPhone vertical).
if (input == "/celular" || input == "/phone")
{
        PhoneWidget::Toggle();
        return;
}

// /menu: abre/fecha o menu "Maximo RP" (sobreposicao; nao usa o Pause nativo, nao desconecta).
if (input == "/menu")
{
        MaximoMenu::Toggle();
        return;
}

if (input == "/cartao" || input.rfind("/cartao ", 0) == 0)
        {
            // cartao de identidade 3D: /cartao | teste [categoria] [id] | anim | limpar | dist|altura|largura <n> | los|proprio on|off
            std::string args = input.size() > 7 ? input.substr(8) : std::string();
            std::string reply = IdCard3D_Command(args);
//            addDebugMessage("(Cliente): %s", reply.c_str());
        }
if (input == "/giroc" || input.rfind("/giroc ", 0) == 0)
        {
            // giroflex (cliente): /giroc | teste | teste off | recarregar
            std::string args = input.size() > 6 ? input.substr(7) : std::string();
            std::string reply = Giroflex_Command(args);
            addDebugMessage("%s", reply.c_str());
            return;
        }
if (input == "/hud" || input.rfind("/hud ", 0) == 0)
        {
            // HUD do Maximo RP: /hud | on | off | teste | reset | escala <n> | set <campo> <valor>
            std::string args = input.size() > 4 ? input.substr(5) : std::string();
            std::string reply = Hud::DebugCommand(args);
            addDebugMessage("%s", reply.c_str());

            return;
        }
if (input == "/camera" || input.rfind("/camera ", 0) == 0 || input == "/cam" || input.rfind("/cam ", 0) == 0)
        {
            // zoom da camera: /camera | /camera <1-3> | /camera pe <1-3> | /camera carro <1-3> | /camera pinca on|off
            size_t sp = input.find(' ');
            std::string args = (sp == std::string::npos) ? std::string() : input.substr(sp + 1);
            std::string reply = CCameraZoom::DebugCommand(args);
            addDebugMessage("%s", reply.c_str());

            return;
        }
if (input == "/roupas" || input.rfind("/roupas ", 0) == 0)
        {
            // tela de roupas em HTML: /roupas (abre) | /roupas fechar
            // le <Android/data/com.maximo.roleplay/files>/SAMP/NET/ROUPAS.HTML
            std::string args = input.size() > 7 ? input.substr(8) : std::string();

            if (args == "fechar")
            {
                pJavaWrapper->HideClothesUI();
                addDebugMessage("[ROUPAS] fechado");
            }
            else
            {
                pJavaWrapper->ShowClothesUI();
                addDebugMessage("[ROUPAS] abrindo SAMP/NET/ROUPAS.HTML");
            }

            return;
        }
if (input == "/controles" || input.rfind("/controles ", 0) == 0 || input == "/controle" || input.rfind("/controle ", 0) == 0)
        {
            // troca do sistema de controles: /controles (abre a tela de escolha) | /controles classico | /controles moderno
            size_t sp = input.find(' ');
            std::string arg = (sp == std::string::npos) ? std::string() : input.substr(sp + 1);
            while (!arg.empty() && arg.back() == ' ') arg.pop_back();
            for (auto& ch : arg) ch = (char)tolower((unsigned char)ch);
            ControlMode::EnsureLoaded();
            if (arg == "classico" || arg == "clássico" || arg == "classic")
            {
                if (CtlLogic::kClassicMaintenance) addDebugMessage("[CONTROLES] O controle classico esta em manutencao. Somente o moderno esta disponivel.");
                else { ControlMode::Set(CtlLogic::Mode::Classic); addDebugMessage("[CONTROLES] Controle classico ativado."); }
            }
            else if (arg == "moderno" || arg == "modern")
            {
                ControlMode::Set(CtlLogic::Mode::Modern);
                addDebugMessage("[CONTROLES] Controle moderno ativado.");
            }
            else if (arg.empty())
            {
                ControlsSelector::Open();
                addDebugMessage("[CONTROLES] Escolha o sistema de controles na tela. Atalho: /controles classico | /controles moderno");
            }
            else
            {
                addDebugMessage("[CONTROLES] Uso: /controles (abre a escolha) | /controles classico | /controles moderno");
            }

            return;
        }
if (input == "/movimento" || input.rfind("/movimento ", 0) == 0)
        {
            // modo de movimento do joystick: /movimento | walk | jog | sprint | reset | ajuste ...
            std::string args = input.size() > 10 ? input.substr(11) : std::string();
            std::string reply = CMoveMode::Command(args);
            addDebugMessage("%s", reply.c_str());

            return;
        }
if (input == "/hiago22")
        {
            TestPlayerClothesDescHG();

            addDebugMessage(
                "[CLOTHES TEST] Verifique o samp.log"
            );

            return;
        }
if (input == "/dp" || input.rfind("/dp ", 0) == 0)
        {
            // diagnostico local de colisao/objeto (CustomDiag.h): /dp ajuda | on | off | marca [texto] | quem
            std::string args = input.size() > 3 ? input.substr(4) : std::string();
            std::string reply = DpDiag::Command(args);
            addDebugMessage("%s", reply.c_str());

            return;
        }
if (input == "/ifp" || input.rfind("/ifp ", 0) == 0)
        {
            // teste de IFP customizado: /ifp ajuda | carregar | lista | tocar | parar | remoto
            std::string args = input.size() > 4 ? input.substr(5) : std::string();
            std::string reply = CustomAnimTest::Command(args);
            addDebugMessage("%s", reply.c_str());

            return;
        }
             if (input[0] == '/')
                pNetGame->SendChatCommand(input.c_str());
                else
                pNetGame->SendChatMessage(input.c_str());
        }

}
