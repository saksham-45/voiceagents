#include "AgentControlView.h"

#include <QHBoxLayout>
#include <QVBoxLayout>
#include <QLabel>
#include <QPushButton>

#include "AgentControl.h"

namespace lmms::gui
{

AgentControlView::AgentControlView( AgentControlPlugin *plugin )
	: ToolPluginView( plugin )
	, m_plugin( plugin )
	, m_input( new QLineEdit( this ) )
	, m_log( new QTextEdit( this ) )
	, m_status( new QLabel( tr( "Listening on 127.0.0.1:7777" ), this ) )
{
	setWindowTitle( tr( "Agent Control" ) );
	setSizePolicy( QSizePolicy::Fixed, QSizePolicy::Fixed );
	m_log->setReadOnly( true );
	m_input->setPlaceholderText( tr( "Try: import kick.wav or add 808" ) );

	auto runBtn = new QPushButton( tr( "Run" ), this );
	auto layout = new QVBoxLayout( this );
	layout->addWidget( m_status );
	auto row = new QHBoxLayout();
	row->addWidget( m_input );
	row->addWidget( runBtn );
	layout->addLayout( row );
	layout->addWidget( m_log );

	connect( runBtn, &QPushButton::clicked, this, &AgentControlView::runCommand );
	connect( m_input, &QLineEdit::returnPressed, this, &AgentControlView::runCommand );
	connect( m_plugin, &AgentControlPlugin::logMessage, this, &AgentControlView::appendLog );
	connect( m_plugin, &AgentControlPlugin::commandResult, this, &AgentControlView::appendLog );

	hide();
	if( parentWidget() )
	{
		parentWidget()->hide();
	}
}

void AgentControlView::runCommand()
{
	const QString text = m_input->text();
	const QString result = m_plugin->handleCommand( text );
	appendLog( text + " => " + result );
}

void AgentControlView::appendLog( const QString &text )
{
	m_log->append( text );
}

} // namespace lmms::gui
