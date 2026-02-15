#include <Geode/Geode.hpp>

// --- WINDOWS NETWORKING SHIELD ---
#ifdef GEODE_IS_WINDOWS
    #ifndef WIN32_LEAN_AND_MEAN
        #define WIN32_LEAN_AND_MEAN
    #endif
    #include <winsock2.h>
    #include <ws2tcpip.h>
#endif

#include <Geode/modify/MenuLayer.hpp>
#include <Geode/modify/LevelEditorLayer.hpp>
#include <Geode/modify/EditorPauseLayer.hpp>
#include "NetworkManager.hpp"

using namespace geode::prelude;

// --- MULTIPLAYER POPUP (Server Browser) ---
class ServerBrowser : public FLAlertLayer {
    CCMenu* m_listMenu;
public:
    bool init() {
        // FIX: Mandatory 9th argument (1.0f) for Geode v5 alpha
        if (!FLAlertLayer::init(nullptr, "LAN Servers", "Searching for hosts...", "Cancel", nullptr, 300.0f, false, 0, 1.0f)) 
            return false;
        
        m_listMenu = CCMenu::create();
        m_listMenu->setLayout(ColumnLayout::create());
        m_listMenu->setPosition({150, 100}); 
        m_mainLayer->addChild(m_listMenu);

        NetworkManager::get()->startSearching();
        this->schedule(schedule_selector(ServerBrowser::refreshList), 1.0f);
        return true;
    }

    void refreshList(float dt) {
        m_listMenu->removeAllChildren();
        auto servers = NetworkManager::get()->getFoundServers();
        if (servers.empty()) {
            auto label = CCLabelBMFont::create("Scanning...", "goldFont.fnt");
            label->setScale(0.6f);
            m_listMenu->addChild(label);
        }
        for (auto& s : servers) {
            std::string labelText = s.name + " (" + s.ip + ")";
            auto btnSprite = ButtonSprite::create(labelText.c_str(), 200, true, "goldFont.fnt", "GJ_button_01.png", 30, 0.6f);
            auto btn = CCMenuItemSpriteExtra::create(btnSprite, this, menu_selector(ServerBrowser::onJoin));
            btn->setUserObject(CCString::create(s.ip));
            m_listMenu->addChild(btn);
        }
        m_listMenu->updateLayout();
    }

    void onJoin(CCObject* sender) {
        auto node = static_cast<CCNode*>(sender);
        if (auto ipStr = dynamic_cast<CCString*>(node->getUserObject())) {
            NetworkManager::get()->connectToServer(ipStr->getCString());
            this->onBtn1(nullptr); 
        }
    }

    static ServerBrowser* create() {
        auto ret = new ServerBrowser();
        if (ret && ret->init()) { ret->autorelease(); return ret; }
        CC_SAFE_DELETE(ret);
        return nullptr;
    }
};

// --- HOOK 1: MAIN MENU WIFI BUTTON ---
class $modify(MyMenuLayer, MenuLayer) {
    bool init() {
        if (!MenuLayer::init()) return false;
        auto menu = this->getChildByID("bottom-menu");
        auto sprite = CCSprite::createWithSpriteFrameName("GJ_everyplayBtn_001.png");
        if (!sprite) sprite = ButtonSprite::create("LAN", 40, true, "goldFont.fnt", "GJ_button_01.png", 30, 0.6f);
        auto btn = CCMenuItemSpriteExtra::create(sprite, this, menu_selector(MyMenuLayer::onMultiplayer));
        menu->addChild(btn);
        menu->updateLayout();
        return true;
    }
    void onMultiplayer(CCObject*) { ServerBrowser::create()->show(); }
};

// --- HOOK 2: EDITOR PAUSE (HOST BUTTON) ---
class $modify(MyPauseLayer, EditorPauseLayer) {
    void customSetup() {
        EditorPauseLayer::customSetup();
        auto menu = this->getChildByID("center-menu");
        auto btnSprite = ButtonSprite::create("Host LAN", 80, true, "goldFont.fnt", "GJ_button_01.png", 30, 0.6f);
        auto btn = CCMenuItemSpriteExtra::create(btnSprite, this, menu_selector(MyPauseLayer::onHost));
        menu->addChild(btn);
        menu->updateLayout();
    }
    void onHost(CCObject*) {
        std::string levelName = "Unknown Level";
        if (auto level = LevelEditorLayer::get()->m_level) levelName = level->m_levelName;
        NetworkManager::get()->startHost(levelName);
    }
};

// --- HOOK 3: LIVE OBJECT SYNC ---
class $modify(MyEditor, LevelEditorLayer) {
    void addObject(GameObject* obj) {
        // FIX: Explicitly call base class method to fix scope errors
        this->LevelEditorLayer::addObject(obj);
        if (obj->getTag() == 99999) return;
        
        std::stringstream ss;
        ss << "1," << obj->m_objectID << "," << obj->getPositionX() << "," << obj->getPositionY();
        NetworkManager::get()->sendPacket(ss.str());
    }
};
