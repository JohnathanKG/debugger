#pragma once

#include <QtWidgets/QTextBrowser>
#include <QtWidgets/QTextEdit>
#include <QtWidgets/QLineEdit>
#include <QtWidgets/QPushButton>
#include "binaryninjaapi.h"
#include "dockhandler.h"
#include "globalarea.h"
#include "viewframe.h"
#include "fontsettings.h"
#include "debuggerapi.h"
//#include "websocket.h"
//#include "file.h"

class DebuggerConsole: public QWidget
{
	Q_OBJECT

	ViewFrame* m_view;
	Ref<BinaryNinjaDebuggerAPI::DebuggerController> m_debugger;

	QLineEdit* m_chatInput;
	QTextEdit* m_chatLog;

	size_t m_debuggerEventCallback;

	void initWebsocket();
	void dataReceived(const std::vector<uint8_t>& data);
	void addMessage(const QString& msg);
	void websocketConnected();
	void websocketDisconnected();
	void websocketError(const QString& msg);
	void storeMessages();

	void sendMessage();

protected:
	void anchorClicked(const QUrl& link);

public:
	DebuggerConsole(QWidget* parent, ViewFrame* view, BinaryViewRef debugger);
	~DebuggerConsole();

	void sendText(const QString& msg);

	void notifyFontChanged();
};

class GlobalConsoleContainer : public GlobalAreaWidget
{
	ViewFrame *m_currentFrame;
	std::map<Ref<BinaryNinjaDebuggerAPI::DebuggerController>, DebuggerConsole*> m_consoleMap;

	QStackedWidget* m_consoleStack;

	//! Get the current active DebuggerConsole. Returns nullptr in the event of an error
	//! or if there is no active ChatBox.
	DebuggerConsole* currentConsole() const;

	//! Delete the DebuggerConsole for the given view.
	void freeDebuggerConsoleForView(QObject*);

public:
	GlobalConsoleContainer(const QString& title);

	//! Send text to the actively-focused ChatBox. If there is no active ChatBox,
	//! no action will be taken.
	void sendText(const QString& msg) const;

	void notifyViewChanged(ViewFrame *) override;
};