#include <QPainter>
#include <QHeaderView>
#include <QLineEdit>
#include <QStatusBar>
#include "debuggerwidget.h"
#include "ui.h"

using namespace BinaryNinjaDebuggerAPI;
using namespace BinaryNinja;
using namespace std;


DebuggerWidget::DebuggerWidget(const QString& name, ViewFrame* view, BinaryViewRef data):
    SidebarWidget(name), m_view(view), m_data(data)
{
    m_controller = DebuggerController::GetController(m_data);

    QVBoxLayout* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);
    layout->setAlignment(Qt::AlignTop);

    m_splitter = new QSplitter(Qt::Vertical, this);
    m_splitter->setChildrenCollapsible(true);

    m_controlsWidget = new DebugControlsWidget(this, "Controls", data);

    m_registersWidget = new DebugRegistersWidget("Native Debugger Registers",
                                                     m_view, m_data);

    m_breakpointsWidget = new DebugBreakpointsWidget("Native Debugger Breakpoints",
                                                                           m_view, m_data, m_menu);

    m_modulesWidget = new DebugModulesWidget("Native Debugger Modules",
                                                                           m_view, m_data);

    m_threadsWidget = new DebugThreadsWidget("Native Debugger Threads",
                                                                           m_view, m_data);

    m_stackWidget = new DebugStackWidget("Native Debugger Stack",
                                                               m_view, m_data);

    auto registerLayout = new QVBoxLayout();
    registerLayout->setContentsMargins(0, 0, 0, 0);
    registerLayout->addWidget(m_registersWidget);

    auto bpLayout = new QVBoxLayout();
    bpLayout->setContentsMargins(0, 0, 0, 0);
    bpLayout->addWidget(m_breakpointsWidget);

    auto modulesLayout = new QVBoxLayout();
    modulesLayout->setContentsMargins(0, 0, 0, 0);
    modulesLayout->addWidget(m_modulesWidget);

    auto threadsLayout = new QVBoxLayout();
    threadsLayout->setContentsMargins(0, 0, 0, 0);
    threadsLayout->addWidget(m_threadsWidget);

    auto stackLayout = new QVBoxLayout();
    stackLayout->setContentsMargins(0, 0, 0, 0);
    stackLayout->addWidget(m_stackWidget);

    m_registersGroup = new ExpandableGroup(registerLayout, "Registers");
    m_breakpointsGroup = new ExpandableGroup(bpLayout, "Breakpoints");
    m_stackGroup = new ExpandableGroup(stackLayout, "Stack");
    m_modulesGroup = new ExpandableGroup(modulesLayout, "Modules");
    m_threadsGroup = new ExpandableGroup(threadsLayout, "Threads");

    m_splitter->addWidget(m_controlsWidget);
    m_splitter->addWidget(m_registersGroup);
    m_splitter->addWidget(m_breakpointsGroup);
    m_splitter->addWidget(m_stackGroup);
    m_splitter->addWidget(m_modulesGroup);
    m_splitter->addWidget(m_threadsGroup);

    layout->addWidget(m_splitter);
    setLayout(layout);

	m_ui = DebuggerUI::GetForViewFrame(view);
	connect(m_ui, &DebuggerUI::debuggerEvent, this, &DebuggerWidget::uiEventHandler);
}


DebuggerWidget::~DebuggerWidget()
{
}


void DebuggerWidget::notifyFontChanged()
{
    LogWarn("font changed");
}


void DebuggerWidget::updateContent()
{
    m_registersWidget->updateContent();
    m_modulesWidget->updateContent();
    m_threadsWidget->updateContent();
    m_stackWidget->updateContent();
}


void DebuggerWidget::uiEventHandler(const DebuggerEvent &event)
{
	m_controlsWidget->updateButtons();
    switch (event.type)
    {
    case TargetStoppedEventType:
        // These updates ensure the widgets become empty after the target stops
    case DetachedEventType:
    case QuitDebuggingEventType:
    case BackEndDisconnectedEventType:
		updateContent();
		break;
	case ActiveThreadChangedEvent:
		m_stackWidget->updateContent();
		break;
    case RelativeBreakpointAddedEvent:
    case AbsoluteBreakpointAddedEvent:
    case RelativeBreakpointRemovedEvent:
    case AbsoluteBreakpointRemovedEvent:
		m_breakpointsWidget->updateContent();
		break;
    default:
        break;
    }
}