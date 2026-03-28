#include "AgentControl.h"

#include <QDir>
#include <QCoreApplication>
#include <QFile>
#include <QFileInfo>
#include <QHostAddress>
#include <QJsonDocument>
#include <QJsonParseError>
#include <QMetaObject>
#include <QStandardPaths>
#include <QTimer>

#include "ConfigManager.h"
#include "Effect.h"
#include "EffectChain.h"
#include "Engine.h"
#include "GuiApplication.h"
#include "ImportFilter.h"
#include "Instrument.h"
#include "InstrumentTrack.h"
#include "LmmsCommonMacros.h"
#include "MainWindow.h"
#include "PluginFactory.h"
#include "SampleClip.h"
#include "SampleTrack.h"
#include "Song.h"
#include "TimePos.h"
#include "Track.h"
#include "plugin_export.h"

#include "AgentControlView.h"

namespace lmms
{

extern "C"
{
Plugin::Descriptor PLUGIN_EXPORT AgentControl_plugin_descriptor =
{
	LMMS_STRINGIFY( PLUGIN_NAME ),
	"Agent Control",
	QT_TRANSLATE_NOOP( "PluginBrowser", "Voice/text command bridge for LMMS" ),
	"codex",
	0x0100,
	Plugin::Type::Tool,
	nullptr,
	nullptr,
	nullptr,
};

PLUGIN_EXPORT Plugin * lmms_plugin_main( Model *, void * )
{
	return new AgentControlPlugin;
}
}

AgentControlService* AgentControlService::instance()
{
	static AgentControlService* service = nullptr;
	if( service == nullptr )
	{
		service = new AgentControlService;
		if( auto *app = QCoreApplication::instance() )
		{
			service->setParent( app );
		}
	}
	return service;
}

AgentControlService::AgentControlService()
{
	connect( &m_server, &QTcpServer::newConnection, this, &AgentControlService::onNewConnection );

	if( !m_server.listen( QHostAddress::LocalHost, 7777 ) )
	{
		emit logMessage( tr( "IPC server failed: %1" ).arg( m_server.errorString() ) );
	}
	else
	{
		emit logMessage( tr( "IPC listening on 127.0.0.1:%1" ).arg( m_server.serverPort() ) );
	}
}

AgentControlService::~AgentControlService()
{
	for( auto *client : m_clients )
	{
		if( client != nullptr )
		{
			client->disconnect( this );
			client->close();
			client->deleteLater();
		}
	}
	m_clients.clear();
	m_server.close();
}

AgentControlPlugin::AgentControlPlugin() :
	ToolPlugin( &AgentControl_plugin_descriptor, nullptr )
{
	auto *svc = service();
	connect( svc, &AgentControlService::logMessage, this, &AgentControlPlugin::logMessage );
	connect( svc, &AgentControlService::commandResult, this, &AgentControlPlugin::commandResult );
}

AgentControlPlugin::~AgentControlPlugin() = default;

AgentControlService* AgentControlPlugin::service()
{
	return AgentControlService::instance();
}

QString AgentControlPlugin::nodeName() const
{
	return AgentControl_plugin_descriptor.name;
}

void AgentControlPlugin::saveSettings( QDomDocument&, QDomElement& )
{
}

void AgentControlPlugin::loadSettings( const QDomElement& )
{
}

gui::PluginView* AgentControlPlugin::instantiateView( QWidget* )
{
	return new gui::AgentControlView( this );
}

QString AgentControlPlugin::handleCommand( const QString& rawText )
{
	return service()->handleCommand( rawText );
}

QString AgentControlPlugin::handleJson( const QJsonObject& obj )
{
	return service()->handleJson( obj );
}

QString AgentControlService::handleCommand( const QString& rawText )
{
	const QString trimmed = rawText.trimmed();
	if( trimmed.isEmpty() )
	{
		return tr( "No command provided" );
	}

	const auto parts = trimmed.split( ' ', Qt::SkipEmptyParts );
	return dispatchTokens( parts, trimmed );
}

QString AgentControlService::handleJson( const QJsonObject& obj )
{
	QStringList tokens;
	const QString command = obj.value( "command" ).toString().trimmed();
	if( command.isEmpty() )
	{
		return tr( "Missing command field" );
	}

	tokens = command.split( ' ', Qt::SkipEmptyParts );

	const QString file = obj.value( "file" ).toString().trimmed();
	if( !file.isEmpty() )
	{
		tokens << file;
	}

	const QString path = obj.value( "path" ).toString().trimmed();
	if( !path.isEmpty() )
	{
		tokens << path;
	}

	const QString plugin = obj.value( "plugin" ).toString().trimmed();
	if( !plugin.isEmpty() )
	{
		tokens << plugin;
	}

	const QString track = obj.value( "track" ).toString().trimmed();
	if( !track.isEmpty() )
	{
		tokens << "to" << track;
	}

	return dispatchTokens( tokens, command );
}

QString AgentControlService::dispatchTokens( const QStringList& tokens, const QString& rawText )
{
	QString result;
	QString error;
	if( tokens.isEmpty() )
	{
		return tr( "No command provided" );
	}

	const QString first = tokens[0].toLower();

	if( first == "new" )
	{
		if( tokens.size() >= 2 && tokens[1].compare( "project", Qt::CaseInsensitive ) == 0 )
		{
			return newProject( result, error ) ? result : error;
		}
		if( tokens.size() >= 3 && tokens[1].compare( "sample", Qt::CaseInsensitive ) == 0 &&
			tokens[2].compare( "track", Qt::CaseInsensitive ) == 0 )
		{
			return createTrack( Track::Type::Sample, result, error ) ? result : error;
		}
		if( tokens.size() >= 3 && tokens[1].compare( "instrument", Qt::CaseInsensitive ) == 0 &&
			tokens[2].compare( "track", Qt::CaseInsensitive ) == 0 )
		{
			return createTrack( Track::Type::Instrument, result, error ) ? result : error;
		}
		if( tokens.size() >= 3 && tokens[1].compare( "automation", Qt::CaseInsensitive ) == 0 &&
			tokens[2].compare( "track", Qt::CaseInsensitive ) == 0 )
		{
			return createTrack( Track::Type::Automation, result, error ) ? result : error;
		}
		if( tokens.size() >= 3 && tokens[1].compare( "instrument", Qt::CaseInsensitive ) == 0 )
		{
			return createInstrumentTrack( joinTokens( tokens, 2 ), result, error ) ? result : error;
		}
	}

	if( first == "show" )
	{
		if( tokens.size() >= 3 && tokens[1].compare( "tool", Qt::CaseInsensitive ) == 0 )
		{
			return showToolCommand( joinTokens( tokens, 2 ), result, error ) ? result : error;
		}
		return showWindowCommand( joinTokens( tokens, 1 ), result, error ) ? result : error;
	}

	if( first == "open" )
	{
		if( tokens.size() >= 2 && tokens[1].compare( "project", Qt::CaseInsensitive ) == 0 )
		{
			return openProject( joinTokens( tokens, 2 ), result, error ) ? result : error;
		}
		if( tokens.size() >= 2 && normalizeName( joinTokens( tokens, 1 ) ) == "slicer" )
		{
			return createInstrumentTrack( "slicert", result, error ) ? result : error;
		}
	}

	if( first == "save" )
	{
		if( tokens.size() >= 2 && tokens[1].compare( "project", Qt::CaseInsensitive ) == 0 )
		{
			if( tokens.size() >= 4 && tokens[2].compare( "as", Qt::CaseInsensitive ) == 0 )
			{
				return saveProjectAs( joinTokens( tokens, 3 ), result, error ) ? result : error;
			}
			return saveProject( result, error ) ? result : error;
		}
	}

	if( first == "import" )
	{
		if( tokens.size() >= 3 && tokens[1].compare( "audio", Qt::CaseInsensitive ) == 0 )
		{
			return importAudioFile( joinTokens( tokens, 2 ), error ) ? tr( "Imported %1" ).arg( joinTokens( tokens, 2 ) ) : error;
		}
		if( tokens.size() >= 3 && tokens[1].compare( "midi", Qt::CaseInsensitive ) == 0 )
		{
			return importProjectFile( joinTokens( tokens, 2 ), error ) ? tr( "Imported MIDI %1" ).arg( joinTokens( tokens, 2 ) ) : error;
		}
		if( tokens.size() >= 3 && tokens[1].compare( "hydrogen", Qt::CaseInsensitive ) == 0 )
		{
			return importProjectFile( joinTokens( tokens, 2 ), error ) ? tr( "Imported Hydrogen project %1" ).arg( joinTokens( tokens, 2 ) ) : error;
		}
		if( tokens.size() >= 2 )
		{
			const QString fileName = joinTokens( tokens, 1 );
			if( importAudioFile( fileName, error ) )
			{
				return tr( "Imported %1" ).arg( fileName );
			}
			if( importFromDownloads( fileName, error ) )
			{
				return tr( "Imported %1" ).arg( fileName );
			}
			return error;
		}
	}

	if( first == "load" || first == "set" )
	{
		if( tokens.size() >= 3 && tokens[1].compare( "instrument", Qt::CaseInsensitive ) == 0 )
		{
			return createInstrumentTrack( joinTokens( tokens, 2 ), result, error ) ? result : error;
		}
	}

	if( first == "add" )
	{
		if( rawText.contains( "808", Qt::CaseInsensitive ) || rawText.contains( "kick", Qt::CaseInsensitive ) )
		{
			return addKickPattern( error ) ? tr( "Added kick pattern" ) : error;
		}
		if( tokens.size() >= 3 && tokens[1].compare( "effect", Qt::CaseInsensitive ) == 0 )
		{
			int toIndex = tokens.indexOf( "to" );
			const QString effectName = toIndex > 2 ? tokens.mid( 2, toIndex - 2 ).join( " " ) : joinTokens( tokens, 2 );
			const QString trackName = toIndex >= 0 ? joinTokens( tokens, toIndex + 1 ) : QString();
			return addEffectToTrack( effectName, trackName, result, error ) ? result : error;
		}
	}

	if( first == "remove" )
	{
		if( tokens.size() >= 3 && tokens[1].compare( "effect", Qt::CaseInsensitive ) == 0 )
		{
			int fromIndex = tokens.indexOf( "from" );
			const QString effectName = fromIndex > 2 ? tokens.mid( 2, fromIndex - 2 ).join( " " ) : joinTokens( tokens, 2 );
			const QString trackName = fromIndex >= 0 ? joinTokens( tokens, fromIndex + 1 ) : QString();
			return removeEffectFromTrack( effectName, trackName, result, error ) ? result : error;
		}
	}

	return tr( "Unknown command: %1" ).arg( rawText );
}

void AgentControlService::onNewConnection()
{
	while( m_server.hasPendingConnections() )
	{
		QTcpSocket *sock = m_server.nextPendingConnection();
		m_clients.insert( sock );
		connect( sock, &QTcpSocket::readyRead, this, &AgentControlService::onSocketReady );
		connect( sock, &QTcpSocket::disconnected, this, &AgentControlService::onSocketClosed );
		emit logMessage( tr( "Client connected" ) );
	}
}

void AgentControlService::onSocketReady()
{
	auto *sock = qobject_cast<QTcpSocket *>( sender() );
	if( !sock )
	{
		return;
	}

	const QByteArray payload = sock->readAll();
	for( const QByteArray &line : payload.split( '\n' ) )
	{
		if( line.trimmed().isEmpty() )
		{
			continue;
		}

		QJsonParseError parseError {};
		const QJsonDocument doc = QJsonDocument::fromJson( line, &parseError );
		const QString result = parseError.error == QJsonParseError::NoError && doc.isObject()
			? handleJson( doc.object() )
			: handleCommand( QString::fromUtf8( line ) );

		emit commandResult( result );
		const QJsonObject reply {{ "result", result }};
		sock->write( QJsonDocument( reply ).toJson( QJsonDocument::Compact ) + "\n" );
		sock->flush();
	}
}

void AgentControlService::onSocketClosed()
{
	auto *sock = qobject_cast<QTcpSocket *>( sender() );
	if( !sock )
	{
		return;
	}
	m_clients.remove( sock );
	sock->deleteLater();
	emit logMessage( tr( "Client disconnected" ) );
}

bool AgentControlService::newProject( QString& result, QString& error )
{
	Song *song = Engine::getSong();
	auto *guiApp = gui::getGUI();
	if( song == nullptr || guiApp == nullptr || guiApp->mainWindow() == nullptr )
	{
		error = tr( "GUI is not ready" );
		return false;
	}
	QTimer::singleShot( 100, guiApp->mainWindow(), [song]()
	{
		song->createNewProject();
	} );
	result = tr( "Queued new project" );
	return true;
}

bool AgentControlService::openProject( const QString& path, QString& result, QString& error )
{
	const QString fullPath = canonicalPath( path );
	if( fullPath.isEmpty() )
	{
		error = tr( "Project not found: %1" ).arg( path );
		return false;
	}
	Song *song = Engine::getSong();
	auto *guiApp = gui::getGUI();
	if( song == nullptr || guiApp == nullptr || guiApp->mainWindow() == nullptr )
	{
		error = tr( "GUI is not ready" );
		return false;
	}
	QTimer::singleShot( 100, guiApp->mainWindow(), [song, fullPath]()
	{
		song->loadProject( fullPath );
	} );
	result = tr( "Queued open project %1" ).arg( QFileInfo( fullPath ).fileName() );
	return true;
}

bool AgentControlService::saveProject( QString& result, QString& error )
{
	Song *song = Engine::getSong();
	if( song == nullptr )
	{
		error = tr( "No active song" );
		return false;
	}
	if( song->projectFileName().isEmpty() )
	{
		error = tr( "Project has no filename. Use save project as <path>" );
		return false;
	}
	if( !song->guiSaveProject() )
	{
		error = tr( "Save failed" );
		return false;
	}
	result = tr( "Saved project %1" ).arg( QFileInfo( song->projectFileName() ).fileName() );
	return true;
}

bool AgentControlService::saveProjectAs( const QString& path, QString& result, QString& error )
{
	Song *song = Engine::getSong();
	if( song == nullptr )
	{
		error = tr( "No active song" );
		return false;
	}
	const QString fullPath = QFileInfo( path ).isAbsolute()
		? QDir::cleanPath( path )
		: QDir::current().absoluteFilePath( path );
	if( !song->guiSaveProjectAs( fullPath ) )
	{
		error = tr( "Save failed for %1" ).arg( fullPath );
		return false;
	}
	result = tr( "Saved project as %1" ).arg( fullPath );
	return true;
}

bool AgentControlService::showWindowCommand( const QString& windowName, QString& result, QString& error )
{
	auto *guiApp = gui::getGUI();
	if( guiApp == nullptr || guiApp->mainWindow() == nullptr )
	{
		error = tr( "GUI is not ready" );
		return false;
	}

	const QString normalized = normalizeName( windowName );
	const char *slot = nullptr;
	if( normalized == "songeditor" )
	{
		slot = "toggleSongEditorWin";
	}
	else if( normalized == "patterneditor" )
	{
		slot = "togglePatternEditorWin";
	}
	else if( normalized == "pianoroll" )
	{
		slot = "togglePianoRollWin";
	}
	else if( normalized == "automationeditor" )
	{
		slot = "toggleAutomationEditorWin";
	}
	else if( normalized == "mixer" )
	{
		slot = "toggleMixerWin";
	}
	else if( normalized == "controllerrack" )
	{
		slot = "toggleControllerRack";
	}
	else if( normalized == "projectnotes" )
	{
		slot = "toggleProjectNotesWin";
	}
	else if( normalized == "microtuner" )
	{
		slot = "toggleMicrotunerWin";
	}
	else
	{
		error = tr( "Unknown window: %1" ).arg( windowName );
		return false;
	}

	bool invoked = false;
	if( normalized == "patterneditor" )
	{
		invoked = QMetaObject::invokeMethod( guiApp->mainWindow(), slot, Qt::DirectConnection,
			Q_ARG( bool, true ) );
	}
	else
	{
		invoked = QMetaObject::invokeMethod( guiApp->mainWindow(), slot, Qt::DirectConnection );
	}

	if( !invoked )
	{
		error = tr( "Failed to show %1" ).arg( windowName );
		return false;
	}
	result = tr( "Toggled %1" ).arg( windowName );
	return true;
}

bool AgentControlService::showToolCommand( const QString& toolName, QString& result, QString& error )
{
	const QString desired = normalizeName( toolName );
	for( const Plugin::Descriptor* desc : getPluginFactory()->descriptors( Plugin::Type::Tool ) )
	{
		const QString byName = normalizeName( QString::fromUtf8( desc->name ) );
		const QString byDisplayName = normalizeName( QString::fromUtf8( desc->displayName ) );
		if( desired != byName && desired != byDisplayName )
		{
			continue;
		}

		auto *tool = ToolPlugin::instantiate( QString::fromUtf8( desc->name ), nullptr );
		if( tool == nullptr )
		{
			error = tr( "Failed to instantiate tool %1" ).arg( toolName );
			return false;
		}
		auto *view = tool->createView( gui::getGUI()->mainWindow() );
		if( view == nullptr )
		{
			delete tool;
			error = tr( "Failed to create view for tool %1" ).arg( toolName );
			return false;
		}
		view->show();
		if( view->parentWidget() )
		{
			view->parentWidget()->show();
		}
		view->setFocus();
		result = tr( "Opened tool %1" ).arg( QString::fromUtf8( desc->displayName ) );
		return true;
	}

	error = tr( "Unknown tool: %1" ).arg( toolName );
	return false;
}

bool AgentControlService::createTrack( Track::Type type, QString& result, QString& error )
{
	Song *song = Engine::getSong();
	if( song == nullptr )
	{
		error = tr( "No active song" );
		return false;
	}

	Track *track = Track::create( type, song );
	if( track == nullptr )
	{
		error = tr( "Failed to create track" );
		return false;
	}

	result = tr( "Created %1" ).arg( track->name() );
	return true;
}

bool AgentControlService::createInstrumentTrack( const QString& pluginName, QString& result, QString& error )
{
	Song *song = Engine::getSong();
	auto *guiApp = gui::getGUI();
	if( song == nullptr || guiApp == nullptr || guiApp->mainWindow() == nullptr )
	{
		error = tr( "GUI is not ready" );
		return false;
	}

	const QString requested = normalizeName( pluginName );
	QString resolvedPlugin;
	QString displayName = pluginName.trimmed();
	for( const Plugin::Descriptor* desc : getPluginFactory()->descriptors( Plugin::Type::Instrument ) )
	{
		const QString byName = normalizeName( QString::fromUtf8( desc->name ) );
		const QString byDisplayName = normalizeName( QString::fromUtf8( desc->displayName ) );
		if( requested == byName || requested == byDisplayName )
		{
			resolvedPlugin = QString::fromUtf8( desc->name );
			displayName = QString::fromUtf8( desc->displayName );
			break;
		}
	}
	if( resolvedPlugin.isEmpty() )
	{
		error = tr( "Unknown instrument: %1" ).arg( pluginName );
		return false;
	}

	QTimer::singleShot( 0, guiApp->mainWindow(), [song, resolvedPlugin]()
	{
		auto *track = dynamic_cast<InstrumentTrack *>( Track::create( Track::Type::Instrument, song ) );
		if( track != nullptr )
		{
			track->loadInstrument( resolvedPlugin );
		}
	} );
	result = tr( "Queued instrument %1" ).arg( displayName );
	return true;
}

bool AgentControlService::importFromDownloads( const QString& fileName, QString& error )
{
	return importAudioFile( resolveDownloadsFile( fileName ), error );
}

bool AgentControlService::importAudioFile( const QString& path, QString& error )
{
	const QString fullPath = canonicalPath( path );
	if( fullPath.isEmpty() )
	{
		error = tr( "File not found: %1" ).arg( path );
		return false;
	}
	auto *track = createSampleTrack( QFileInfo( fullPath ).fileName() );
	if( track == nullptr )
	{
		error = tr( "Failed to create sample track" );
		return false;
	}

	auto *clip = dynamic_cast<SampleClip *>( track->createClip( TimePos( 0 ) ) );
	if( clip == nullptr )
	{
		error = tr( "Failed to create sample clip" );
		return false;
	}

	track->addClip( clip );
	clip->setSampleFile( fullPath );
	clip->updateLength();
	return true;
}

bool AgentControlService::importProjectFile( const QString& path, QString& error )
{
	const QString fullPath = canonicalPath( path );
	if( fullPath.isEmpty() )
	{
		error = tr( "File not found: %1" ).arg( path );
		return false;
	}
	Song *song = Engine::getSong();
	if( song == nullptr )
	{
		error = tr( "No active song" );
		return false;
	}
	ImportFilter::import( fullPath, song );
	return true;
}

bool AgentControlService::addKickPattern( QString& error )
{
	const QString samplePath = defaultKickSample();
	if( samplePath.isEmpty() )
	{
		error = tr( "Kick sample missing" );
		return false;
	}

	auto *track = createSampleTrack( tr( "Agent 808" ) );
	if( track == nullptr )
	{
		error = tr( "Failed to create sample track" );
		return false;
	}

	const int stepTicks = DefaultTicksPerBar / DefaultStepsPerBar;
	const int steps[] = { 0, 4, 8, 12 };
	for( int s : steps )
	{
		addSampleClip( track, samplePath, s * stepTicks );
	}
	return true;
}

bool AgentControlService::addEffectToTrack( const QString& effectName, const QString& trackName, QString& result, QString& error )
{
	Track *track = trackName.isEmpty()
		? findLastTrackOfTypes( { Track::Type::Instrument, Track::Type::Sample } )
		: findTrackByName( trackName );
	if( track == nullptr )
	{
		error = tr( "Track not found: %1" ).arg( trackName.isEmpty() ? tr( "last audio track" ) : trackName );
		return false;
	}

	if( effectChainForTrack( track ) == nullptr )
	{
		error = tr( "Track %1 does not support effects" ).arg( track->name() );
		return false;
	}

	QString resolvedEffect;
	QString displayName = effectName.trimmed();
	const QString requested = normalizeName( effectName );
	for( const Plugin::Descriptor* desc : getPluginFactory()->descriptors( Plugin::Type::Effect ) )
	{
		const QString byName = normalizeName( QString::fromUtf8( desc->name ) );
		const QString byDisplayName = normalizeName( QString::fromUtf8( desc->displayName ) );
		if( requested == byName || requested == byDisplayName )
		{
			resolvedEffect = QString::fromUtf8( desc->name );
			displayName = QString::fromUtf8( desc->displayName );
			break;
		}
	}
	if( resolvedEffect.isEmpty() )
	{
		error = tr( "Unknown effect: %1" ).arg( effectName );
		return false;
	}

	const QString targetTrackName = track->name();
	auto *guiApp = gui::getGUI();
	if( guiApp == nullptr || guiApp->mainWindow() == nullptr )
	{
		error = tr( "GUI is not ready" );
		return false;
	}

	QTimer::singleShot( 0, guiApp->mainWindow(), [this, resolvedEffect, targetTrackName]()
	{
		Track *targetTrack = findTrackByName( targetTrackName );
		if( targetTrack == nullptr )
		{
			return;
		}

		EffectChain *chain = effectChainForTrack( targetTrack );
		if( chain == nullptr )
		{
			return;
		}

		if( auto *effect = Effect::instantiate( resolvedEffect, chain, nullptr ) )
		{
			chain->appendEffect( effect );
		}
	} );
	result = tr( "Queued effect %1 for %2" ).arg( displayName, targetTrackName );
	return true;
}

bool AgentControlService::removeEffectFromTrack( const QString& effectName, const QString& trackName, QString& result, QString& error )
{
	Track *track = trackName.isEmpty()
		? findLastTrackOfTypes( { Track::Type::Instrument, Track::Type::Sample } )
		: findTrackByName( trackName );
	if( track == nullptr )
	{
		error = tr( "Track not found: %1" ).arg( trackName.isEmpty() ? tr( "last audio track" ) : trackName );
		return false;
	}

	EffectChain *chain = effectChainForTrack( track );
	if( chain == nullptr )
	{
		error = tr( "Track %1 does not support effects" ).arg( track->name() );
		return false;
	}

	const QString requested = normalizeName( effectName );
	for( Effect *effect : chain->effects() )
	{
		const QString byName = normalizeName( QString::fromUtf8( effect->descriptor()->name ) );
		const QString byDisplayName = normalizeName( effect->displayName() );
		if( requested == byName || requested == byDisplayName )
		{
			const QString removedName = effect->displayName();
			chain->removeEffect( effect );
			delete effect;
			result = tr( "Removed effect %1 from %2" ).arg( removedName, track->name() );
			return true;
		}
	}

	error = tr( "Effect %1 not found on %2" ).arg( effectName, track->name() );
	return false;
}

Track* AgentControlService::findTrackByName( const QString& trackName ) const
{
	Song *song = Engine::getSong();
	if( song == nullptr )
	{
		return nullptr;
	}

	const QString wanted = normalizeName( trackName );
	const auto &tracks = song->tracks();
	for( auto it = tracks.rbegin(); it != tracks.rend(); ++it )
	{
		if( normalizeName( (*it)->name() ) == wanted )
		{
			return *it;
		}
	}
	return nullptr;
}

Track* AgentControlService::findLastTrackOfTypes( const QList<Track::Type>& types ) const
{
	Song *song = Engine::getSong();
	if( song == nullptr )
	{
		return nullptr;
	}

	const auto &tracks = song->tracks();
	for( auto it = tracks.rbegin(); it != tracks.rend(); ++it )
	{
		if( types.contains( (*it)->type() ) )
		{
			return *it;
		}
	}
	return nullptr;
}

InstrumentTrack* AgentControlService::findInstrumentTrack( const QString& trackName ) const
{
	return dynamic_cast<InstrumentTrack *>( findTrackByName( trackName ) );
}

SampleTrack* AgentControlService::createSampleTrack( const QString& name ) const
{
	Song *song = Engine::getSong();
	if( song == nullptr )
	{
		return nullptr;
	}
	auto *track = dynamic_cast<SampleTrack *>( Track::create( Track::Type::Sample, song ) );
	if( track != nullptr && !name.isEmpty() )
	{
		track->setName( name );
	}
	return track;
}

EffectChain* AgentControlService::effectChainForTrack( Track* track ) const
{
	if( auto *instrumentTrack = dynamic_cast<InstrumentTrack *>( track ) )
	{
		return instrumentTrack->audioBusHandle()->effects();
	}
	if( auto *sampleTrack = dynamic_cast<SampleTrack *>( track ) )
	{
		return sampleTrack->audioBusHandle()->effects();
	}
	return nullptr;
}

bool AgentControlService::addSampleClip( SampleTrack *track, const QString& samplePath, int tickPos )
{
	auto *clip = dynamic_cast<SampleClip *>( track->createClip( TimePos( tickPos ) ) );
	if( clip == nullptr )
	{
		return false;
	}
	track->addClip( clip );
	clip->setSampleFile( samplePath );
	clip->updateLength();
	return true;
}

QString AgentControlService::resolveDownloadsFile( const QString& fileName ) const
{
	if( fileName.isEmpty() )
	{
		return {};
	}

	QString downloads = QStandardPaths::writableLocation( QStandardPaths::DownloadLocation );
	if( downloads.isEmpty() )
	{
		downloads = QDir::homePath() + "/Downloads";
	}
	const QString absPath = QDir( downloads ).filePath( fileName );
	return QFile::exists( absPath ) ? absPath : QString{};
}

QString AgentControlService::defaultKickSample() const
{
	const QString base = ConfigManager::inst()->factorySamplesDir();
	const QString candidate = QDir( base ).filePath( "drums/bassdrum04.ogg" );
	return QFile::exists( candidate ) ? candidate : QString{};
}

QString AgentControlService::canonicalPath( const QString& path ) const
{
	if( path.isEmpty() )
	{
		return {};
	}

	const QFileInfo absoluteInfo( path );
	if( absoluteInfo.isAbsolute() && absoluteInfo.exists() )
	{
		return absoluteInfo.canonicalFilePath();
	}

	const QString fromDownloads = resolveDownloadsFile( path );
	if( !fromDownloads.isEmpty() )
	{
		return fromDownloads;
	}

	QFileInfo relativeInfo( QDir::current().absoluteFilePath( path ) );
	if( relativeInfo.exists() )
	{
		return relativeInfo.canonicalFilePath();
	}

	return {};
}

QString AgentControlService::joinTokens( const QStringList& tokens, int startIndex ) const
{
	return startIndex < tokens.size() ? tokens.mid( startIndex ).join( " " ).trimmed() : QString{};
}

QString AgentControlService::normalizeName( const QString& text ) const
{
	QString out;
	out.reserve( text.size() );
	for( const QChar ch : text.toLower() )
	{
		if( ch.isLetterOrNumber() )
		{
			out.append( ch );
		}
	}
	return out;
}

} // namespace lmms
