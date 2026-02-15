#include <Geode/Geode.hpp>
#include <Geode/modify/MenuLayer.hpp>
#include <Geode/modify/LevelEditorLayer.hpp>
#include <Geode/modify/EditorPauseLayer.hpp>
#include "NetworkManager.hpp"

using namespace geode::prelude;

// --- SERVER BROWSER POPUP ---
class ServerBrowser : public FLAlertLayer, public FLAlertLayerProtocol {
    CCMenu* m_listMenu;
public:
    bool init() {
        if (!FLAlertLayer::init(nullptr, "LAN Servers", "Searching...", "Cancel", nullptr)) return false;
        
        m_listMenu = CCMenu::create();
        m_listMenu->setLayout(ColumnLayout::create());
        m_listMenu->setPosition({220, 140}); // Center-ish
        m_mainLayer->addChild(m_listMenu);

        // Start listening for beacons
        NetworkManager::get()->startSearching();
        
        // Refresh list every second
        this->schedule(schedule_selector(ServerBrowser::refreshList), 1.0f);
        return true;
    }

    void refreshList(float) {
        m_listMenu->removeAllChildren();
        auto servers = NetworkManager::get()->getFoundServers();

        if (servers.empty()) {
            auto label = CCLabelBMFont::create("Scanning for Hosts...", "goldFont.fnt");
            label->setScale(0.6f);
            m_listMenu->addChild(label);
        }

        for (auto& s : servers) {
            auto btn = CCMenuItemSpriteExtra::create(
                ButtonSprite::create(fmt::format("{} ({})", s.name, s.ip).c_str(), 0, false, "goldFont.fnt", "GJ_button_01.png", 0, 0.8f),
                this,
                menu_selector(ServerBrowser::onJoin)
            );
            btn->setUserObject(CCString::create(s.ip)); // Store IP in button
            m_listMenu->addChild(btn);
        }
        m_listMenu->updateLayout();
    }

    void onJoin(CCObject* sender) {
        auto ip = static_cast<CCString*>(static_cast<CCNode*>(sender)->getUserObject())->getCString();
        NetworkManager::get()->connectToServer(ip);
        this->onBtn1(nullptr); // Close popup
    }

    static ServerBrowser* create() {
        auto ret = new ServerBrowser();
        if (ret && ret->init()) { ret->autorelease(); return ret; }
        delete ret; return nullptr;
    }
};

// --- HOOK: MAIN MENU BUTTON ---
class $modify(MyMenuLayer, MenuLayer) {
    bool init() {
        if (!MenuLayer::init()) return false;

        auto menu = this->getChildByID("bottom-menu");
        
        // Add the Multiplayer Button (Using a standard sprite for now)
        auto btn = CCMenuItemSpriteExtra::create(
            CCSprite::createWithSpriteFrameName("GJ_everyplayBtn_001.png"), // Looks like a wifi icon
            this,
            menu_selector(MyMenuLayer::onMultiplayer)
        );
        
        menu->addChild(btn);
        menu->updateLayout();
        return true;
    }

    void onMultiplayer(CCObject*) {
        ServerBrowser::create()->show();
    }
};

// --- HOOK: HOSTING FROM EDITOR ---
class $modify(MyPauseLayer, EditorPauseLayer) {
    void customSetup() {
        EditorPauseLayer::customSetup();
        auto menu = this->getChildByID("center-menu");
        auto btn = CCMenuItemSpriteExtra::create(ButtonSprite::create("Host LAN"), this, menu_selector(MyPauseLayer::onHost));
        menu->addChild(btn);
        menu->updateLayout();
    }

    void onHost(CCObject*) {
        // Get Level Name
        std::string levelName = "Unknown Level";
        if (auto level = LevelEditorLayer::get()->m_level) {
            levelName = level->m_levelName;
        }
        NetworkManager::get()->startHost(levelName);
    }
};

// --- HOOK: SYNCING ---
class $modify(MyEditor, LevelEditorLayer) {
    void addObject(GameObject* obj) {
        LevelEditorLayer::addObject(obj);
        if (obj->getTag() == 99999) return;
        
        // Protocol: 1,ID,X,Y
        std::string packet = fmt::format("1,{},{},{}", obj->m_objectID, obj->getPositionX(), obj->getPositionY());
        NetworkManager::get()->sendPacket(packet);
    }
    
    // TODO: Hook updateLevelSettings to sync colors (Packet Type 2)
};