#ifndef LMMS_AGENT_CONTROL_VIEW_H
#define LMMS_AGENT_CONTROL_VIEW_H

#include <QTextEdit>
#include <QLineEdit>
#include <QLabel>
#include <QPushButton>

#include "ToolPluginView.h"

namespace lmms
{
class AgentControlPlugin;

namespace gui
{

class AgentControlView : public ToolPluginView
{
	Q_OBJECT
public:
	explicit AgentControlView( AgentControlPlugin *plugin );

private slots:
	void runCommand();
	void appendLog( const QString &text );

private:
	AgentControlPlugin *m_plugin;
	QLineEdit *m_input;
	QTextEdit *m_log;
	QLabel *m_status;
};

} // namespace gui

} // namespace lmms

#endif // LMMS_AGENT_CONTROL_VIEW_H
