#include "../gui.h"
#include "../../main.h"
#include "../../game/game.h"
#include "../../net/netgame.h"
#include <algorithm>
#include "../settings.h"
#include "java/jniutil.h"
#include "../../game/sprite2d.h"
#include "../../game/ClothesTest.h"
#include "../../game/ClothesSync.h"
#include "../../game/Hose.h"
#include "../../game/IdCard3D.h"
#include "../../game/CustomSkin.h"
#include "../../game/camerazoom.h"
#include "../../game/CustomAnimTest.h"
#include "../../game/movemode.h"
extern UI* pUI;
extern CGame* pGame;
extern CNetGame* pNetGame;
extern CSettings* pSettings;
extern CJavaWrapper *pJavaWrapper;

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
if (input == "/mangueira" || input.rfind("/mangueira ", 0) == 0)
        {
            // mangueira de parede: /mangueira a | pegar | soltar | limpar | config ...
            std::string args = input.size() > 10 ? input.substr(11) : std::string();
            std::string reply = Hose_Command(args);
            addDebugMessage("%s", reply.c_str());

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
            addDebugMessage("(Cliente): %s", reply.c_str());
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
            // le <Android/data/com.samp.mobile/files>/SAMP/NET/ROUPAS.HTML
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
