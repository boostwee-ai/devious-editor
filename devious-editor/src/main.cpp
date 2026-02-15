#include <Geode/Geode.hpp>
#include <Geode/modify/MenuLayer.hpp>
#include <Geode/modify/LevelEditorLayer.hpp>
#include <Geode/modify/EditorPauseLayer.hpp>
#include "NetworkManager.hpp"

using namespace geode::prelude;

// --- SERVER BROWSER POPUP ---
class ServerBrowser : public FLAlertLayer {
    CCMenu* m_listMenu;

public:
    // We strictly match the init signature
    bool init() {
        // Init with standard width/height args to prevent ambiguity
        if (!FLAlertLayer::init(nullptr, "LAN Servers", "Searching for hosts...", "Cancel", nullptr, 300.0f, false, 0)) 
            return false;
        
        m_listMenu = CCMenu::create();
        m_listMenu->setLayout(ColumnLayout::create());
        m_listMenu->setPosition({150, 100}); // Manual center adjustment relative to layer
        m_mainLayer->addChild(m_listMenu);

        // Start scanning
        NetworkManager::get()->startSearching();
        
        // Schedule update
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
            
            // EXPLICIT ButtonSprite creation to fix the error
            // (Text, Width, Absolute, Font, Texture, Height, Scale)
            auto btnSprite = ButtonSprite::create(
                labelText.c_str(), 
                200, true, "goldFont.fnt", "GJ_button_01.png", 30, 0.6f
            );

            auto btn = CCMenuItemSpriteExtra::create(
                btnSprite,
                this,
                menu_selector(ServerBrowser::onJoin)
            );
            btn->setUserObject(CCString::create(s.ip));
            m_listMenu->addChild(btn);
        }
        m_listMenu->updateLayout();
    }

    void onJoin(CCObject* sender) {
        auto node = static_cast<CCNode*>(sender);
        // Safe cast check
        if (auto ipStr = dynamic_cast<CCString*>(node->getUserObject())) {
            NetworkManager::get()->connectToServer(ipStr->getCString());
            this->onBtn1(nullptr); // Close popup
        }
    }

    static ServerBrowser* create() {
        auto ret = new ServerBrowser();
        if (ret && ret->init()) { 
            ret->autorelease(); 
            return ret; 
        }
        CC_SAFE_DELETE(ret); // Safer deletion
        return nullptr;
    }
};

// --- HOOK 1: MAIN MENU WIFI BUTTON ---
class $modify(MyMenuLayer, MenuLayer) {
    bool init() {
        if (!MenuLayer::init()) return false;

        auto menu = this->getChildByID("bottom-menu");
        
        // Safe sprite creation
        auto sprite = CCSprite::createWithSpriteFrameName("GJ_everyplayBtn_001.png");
        if (!sprite) {
             // Fallback if texture is missing
             sprite = ButtonSprite::create("LAN", 40, true, "goldFont.fnt", "GJ_button_01.png", 30, 0.6f);
        }

        auto btn = CCMenuItemSpriteExtra::create(
            sprite,
            this,
            menu_selector(MyMenuLayer::onMultiplayer)
        );
        
        // Position it nicely if possible, or let layout handle it
        menu->addChild(btn);
        menu->updateLayout();
        return true;
    }

    void onMultiplayer(CCObject*) {
        ServerBrowser::create()->show();
    }
};

// --- HOOK 2: EDITOR HOST BUTTON ---
class $modify(MyPauseLayer, EditorPauseLayer) {
    void customSetup() {
        EditorPauseLayer::customSetup();
        auto menu = this->getChildByID("center-menu");
        
        // Explicit ButtonSprite
        auto btnSprite = ButtonSprite::create("Host LAN", 80, true, "goldFont.fnt", "GJ_button_01.png", 30, 0.6f);
        
        auto btn = CCMenuItemSpriteExtra::create(
            btnSprite, 
            this, 
            menu_selector(MyPauseLayer::onHost)
        );
        
        menu->addChild(btn);
        menu->updateLayout();
    }

    void onHost(CCObject*) {
        std::string levelName = "Unknown Level";
        if (auto level = LevelEditorLayer::get()->m_level) {
            levelName = level->m_levelName;
        }
        NetworkManager::get()->startHost(levelName);
    }
};

// --- HOOK 3: EDITOR OBJECT SYNC ---
class $modify(MyEditor, LevelEditorLayer) {
    void addObject(GameObject* obj) {
        LevelEditorLayer::addObject(obj);
        if (obj->getTag() == 99999) return;
        
        // Explicit string formatting
        std::stringstream ss;
        ss << "1," << obj->m_objectID << "," << obj->getPositionX() << "," << obj->getPositionY();
        NetworkManager::get()->sendPacket(ss.str());
    }
};