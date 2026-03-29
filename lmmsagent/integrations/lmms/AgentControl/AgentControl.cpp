#include "AgentControl.h"

#include <QDir>
#include <QCoreApplication>
#include <QFile>
#include <QFileInfo>
#include <QHostAddress>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonParseError>
#include <QMetaObject>
#include <QRegularExpression>
#include <QStandardPaths>
#include <QTimer>
#include <QUuid>

#include "ConfigManager.h"
#include "Clip.h"
#include "Effect.h"
#include "EffectChain.h"
#include "Engine.h"
#include "GuiApplication.h"
#include "ImportFilter.h"
#include "Instrument.h"
#include "InstrumentTrack.h"
#include "LmmsCommonMacros.h"
#include "MainWindow.h"
#include "MidiClip.h"
#include "Note.h"
#include "PluginFactory.h"
#include "ProjectJournal.h"
#include "SampleClip.h"
#include "SampleTrack.h"
#include "Song.h"
#include "TimePos.h"
#include "Track.h"
#include "plugin_export.h"

#include "AgentControlView.h"

#include <set>

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
	m_readBuffers.clear();
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
	const QJsonObject response = handleRequest( obj );
	return toCommandResponseText( response );
}

QJsonObject AgentControlService::handleRequest( const QJsonObject& obj )
{
	if( obj.contains( "tool" ) )
	{
		const QString toolName = obj.value( "tool" ).toString().trimmed();
		const QJsonObject args = obj.value( "args" ).toObject();
		if( toolName.isEmpty() )
		{
			return errorResponse( "missing_tool", tr( "Missing tool field" ) );
		}
		return dispatchTool( toolName, args );
	}

	QStringList tokens;
	const QString command = obj.value( "command" ).toString().trimmed();
	if( command.isEmpty() )
	{
		return errorResponse( "missing_command", tr( "Missing command field" ) );
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

	return successResponse( QJsonObject
	{
		{ "message", dispatchTokens( tokens, command ) }
	} );
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
	const QString normalizedRaw = normalizeName( rawText );
	auto containsPhrase = [&normalizedRaw, this]( const QString& phrase )
	{
		return normalizedRaw.contains( normalizeName( phrase ) );
	};

	auto beginnerHelpText = []()
	{
		return QString(
			"Beginner commands:\n"
			"- set tempo to 140\n"
			"- open mixer | open song editor | open piano roll\n"
			"- create instrument track | create sample track | create automation track\n"
			"- load instrument triple oscillator\n"
			"- add effect reverb | remove effect reverb\n"
			"- mute track <name> | solo track <name> | undo\n"
			"- manual map | manual map automation | manual map samples\n"
			"- compose from score sheet | add rhythm | sample as melodic instrument\n"
		"- sidechain through mixer | edit existing song | export song" );
	};

	if( ( first == "agent" && tokens.size() >= 2 && tokens[1].compare( "version", Qt::CaseInsensitive ) == 0 ) ||
		( first == "version" ) )
	{
		return tr( "AgentControl build %1 %2 (direct parser enabled: tempo, beginner help, manual map, playbook aliases)" )
			.arg( QString::fromLatin1( __DATE__ ) )
			.arg( QString::fromLatin1( __TIME__ ) );
	}

	auto manualMapText = []( const QString& area )
	{
		const QString key = area.trimmed().toLower();
		if( key.contains( "automation" ) )
		{
			return QString(
				"Manual map (Automation):\n"
				"- Automated now: create automation track, snapshots, undo, rollback\n"
				"- Guided now: draw curves, controller links, song-global automation\n"
				"- Deferred: typed automation point editing API" );
		}
		if( key.contains( "sample" ) )
		{
			return QString(
				"Manual map (Samples):\n"
				"- Automated now: create/load sample track, import audio/midi/hydrogen, search project audio\n"
				"- Guided now: sample prep and browser audition workflow\n"
				"- Deferred: typed trim/slice/normalize APIs" );
		}
		if( key.contains( "song" ) || key.contains( "track" ) )
		{
			return QString(
				"Manual map (Song Editor):\n"
				"- Automated now: create/rename/select/mute/solo tracks, create pattern\n"
				"- Guided now: timeline clip gestures (move/split/clone)\n"
				"- Deferred: typed clip-editing API" );
		}
		return QString(
			"Manual map (LMMS 0.4.12 compatibility):\n"
			"- Main window/navigation\n"
			"- Song editor/tracks\n"
			"- Piano roll composition\n"
			"- Beat+Bassline rhythm\n"
			"- Instruments/presets\n"
			"- FX mixer/effects\n"
			"- Automation/controllers\n"
			"- Import/samples\n"
			"Try: manual map song editor | manual map automation | manual map samples" );
	};
	const bool mentionsTempo = tokens.contains( "tempo", Qt::CaseInsensitive ) ||
		tokens.contains( "bpm", Qt::CaseInsensitive );
	auto parseFirstInteger = []( const QStringList& parts, int startIndex, int& valueOut )
	{
		const QRegularExpression intPattern( R"((-?\d+))" );
		for( int i = qMax( 0, startIndex ); i < parts.size(); ++i )
		{
			const auto match = intPattern.match( parts[i] );
			if( match.hasMatch() )
			{
				bool ok = false;
				const int parsed = match.captured( 1 ).toInt( &ok );
				if( ok )
				{
					valueOut = parsed;
					return true;
				}
			}
		}
		return false;
	};

	// Keep direct command mode usable for quick tempo changes from the plugin UI.
	// Natural-language planning still lives in the text/voice agent.
	if( mentionsTempo &&
		( first == "set" || first == "increase" || first == "decrease" ||
		  first == "raise" || first == "lower" || first == "tempo" ) )
	{
		const int toIndex = tokens.indexOf( "to" );
		const int byIndex = tokens.indexOf( "by" );
		const bool isDecrease = ( first == "decrease" || first == "lower" );
		const bool isRelative = ( first == "increase" || first == "decrease" || first == "raise" || first == "lower" );

		int targetTempo = 0;
		bool hasTarget = false;

		if( first == "tempo" )
		{
			hasTarget = parseFirstInteger( tokens, 1, targetTempo );
		}
		else if( isRelative && byIndex >= 0 )
		{
			int delta = 0;
			hasTarget = parseFirstInteger( tokens, byIndex + 1, delta );
			if( hasTarget )
			{
				if( const auto *song = Engine::getSong() )
				{
					targetTempo = song->getTempo() + ( isDecrease ? -delta : delta );
				}
				else
				{
					hasTarget = false;
				}
			}
		}
		else if( toIndex >= 0 || first == "set" || isRelative )
		{
			hasTarget = parseFirstInteger( tokens, toIndex >= 0 ? toIndex + 1 : 1, targetTempo );
		}

		if( !hasTarget )
		{
			return tr( "Could not parse tempo. Try: set tempo to 140" );
		}

		QJsonObject tempoResult;
		if( !setTempoValue( targetTempo, tempoResult, error ) )
		{
			return error;
		}
		return tr( "Set tempo to %1" ).arg( tempoResult.value( "tempo_after" ).toInt( targetTempo ) );
	}

	if( containsPhrase( "beginner help" ) )
	{
		return beginnerHelpText();
	}

	if( first == "manual" && tokens.size() >= 2 && tokens[1].compare( "map", Qt::CaseInsensitive ) == 0 )
	{
		return manualMapText( joinTokens( tokens, 2 ) );
	}

	if( first == "list" && tokens.size() >= 2 && tokens[1].compare( "windows", Qt::CaseInsensitive ) == 0 )
	{
		return QString(
			"Openable windows:\n"
			"- song editor\n"
			"- piano roll\n"
			"- automation editor\n"
			"- mixer (aliases: fx mixer, fx mixer/effects, effects)\n"
			"- controller rack\n"
			"- project notes\n"
			"- microtuner" );
	}

	if( containsPhrase( "compose from score sheet" ) )
	{
		QJsonObject tempoResult;
		if( !setTempoValue( 140, tempoResult, error ) )
		{
			return error;
		}
		QString trackResult;
		if( !createTrack( Track::Type::Instrument, trackResult, error ) )
		{
			return error;
		}
		QString instrumentResult;
		if( !createInstrumentTrack( "tripleoscillator", instrumentResult, error ) )
		{
			return error;
		}
		QJsonObject patternResult;
		if( !createPatternClip( QJsonObject(), patternResult, error ) )
		{
			return tr( "Started score-sheet workflow: tempo 140, instrument ready. Next: open piano roll and enter notes." );
		}
		return tr( "Started score-sheet workflow: tempo 140, TripleOscillator loaded, first pattern created." );
	}

	if( containsPhrase( "add rhythm" ) )
	{
		QString trackResult;
		if( !createTrack( Track::Type::Sample, trackResult, error ) )
		{
			return error;
		}
		if( !addKickPattern( error ) )
		{
			return tr( "Created sample track. Next: load drum samples and program BB rhythm." );
		}
		return tr( "Created sample track and added starter kick pattern." );
	}

	if( containsPhrase( "sample as melodic instrument" ) )
	{
		QString trackResult;
		if( !createTrack( Track::Type::Instrument, trackResult, error ) )
		{
			return error;
		}
		QString instrumentResult;
		if( !createInstrumentTrack( "audiofileprocessor", instrumentResult, error ) )
		{
			return tr( "Created instrument track. Next: load AudioFileProcessor manually and set root note." );
		}
		return tr( "Created sample-instrument track with AudioFileProcessor. Next: load sample and test pitch range." );
	}

	if( containsPhrase( "sidechain through mixer" ) )
	{
		if( !showWindowCommand( "mixer", result, error ) )
		{
			return error;
		}
		return tr( "Opened mixer. Next: route source/target, bind controller to gain, then tune depth/release." );
	}

	if( containsPhrase( "edit existing song" ) )
	{
		QJsonObject snapshotResult;
		if( !createSnapshot( "before_existing_song_edit", snapshotResult, error ) )
		{
			return error;
		}
		QString songEditorResult;
		showWindowCommand( "song editor", songEditorResult, error );
		QString mixerResult;
		showWindowCommand( "mixer", mixerResult, error );
		return tr( "Snapshot created. Opened Song Editor and Mixer for edit pass." );
	}

	if( containsPhrase( "export song" ) || containsPhrase( "render song" ) )
	{
		return tr( "Use File -> Export (Ctrl+E). Choose format and destination, then render." );
	}

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
	if( first == "create" )
	{
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
		if( tokens.size() >= 3 && tokens[1].compare( "pattern", Qt::CaseInsensitive ) == 0 &&
			tokens[2].compare( "track", Qt::CaseInsensitive ) == 0 )
		{
			return createTrack( Track::Type::Pattern, result, error ) ? result : error;
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
		if( tokens.size() >= 2 )
		{
			return showWindowCommand( joinTokens( tokens, 1 ), result, error ) ? result : error;
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
		if( rawText.contains( "piano", Qt::CaseInsensitive ) )
		{
			QString createResult;
			if( createInstrumentTrack( "tripleoscillator", createResult, error ) )
			{
				return tr( "Queued piano instrument (TripleOscillator)" );
			}
			return error;
		}
		if( rawText.contains( "808", Qt::CaseInsensitive ) || rawText.contains( "kick", Qt::CaseInsensitive ) )
		{
			return addKickPattern( error ) ? tr( "Added kick pattern" ) : error;
		}
		if( rawText.contains( "snare", Qt::CaseInsensitive ) )
		{
			return addSnarePattern( error ) ? tr( "Added snare pattern" ) : error;
		}
		if( tokens.size() >= 3 && tokens[1].compare( "effect", Qt::CaseInsensitive ) == 0 )
		{
			int toIndex = tokens.indexOf( "to" );
			const QString effectName = toIndex > 2 ? tokens.mid( 2, toIndex - 2 ).join( " " ) : joinTokens( tokens, 2 );
			const QString trackName = toIndex >= 0 ? joinTokens( tokens, toIndex + 1 ) : QString();
			return addEffectToTrack( effectName, trackName, result, error ) ? result : error;
		}
	}

	if( first == "make" )
	{
		if( rawText.contains( "drums", Qt::CaseInsensitive ) &&
			rawText.contains( "hit", Qt::CaseInsensitive ) &&
			rawText.contains( "harder", Qt::CaseInsensitive ) )
		{
			return addKickPattern( error ) ? tr( "Added kick pattern" ) : error;
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

	if( first == "mute" )
	{
		const QString trackName = ( tokens.size() >= 2 && tokens[1].compare( "track", Qt::CaseInsensitive ) == 0 ) ?
			joinTokens( tokens, 2 ) : joinTokens( tokens, 1 );
		QJsonObject muteResult;
		if( setTrackMute( trackName, true, muteResult, error ) )
		{
			return tr( "Muted track %1" ).arg( muteResult.value( "track_name" ).toString( trackName ) );
		}
		return error;
	}

	if( first == "unmute" )
	{
		const QString trackName = ( tokens.size() >= 2 && tokens[1].compare( "track", Qt::CaseInsensitive ) == 0 ) ?
			joinTokens( tokens, 2 ) : joinTokens( tokens, 1 );
		QJsonObject muteResult;
		if( setTrackMute( trackName, false, muteResult, error ) )
		{
			return tr( "Unmuted track %1" ).arg( muteResult.value( "track_name" ).toString( trackName ) );
		}
		return error;
	}

	if( first == "solo" )
	{
		const QString trackName = ( tokens.size() >= 2 && tokens[1].compare( "track", Qt::CaseInsensitive ) == 0 ) ?
			joinTokens( tokens, 2 ) : joinTokens( tokens, 1 );
		QJsonObject soloResult;
		if( setTrackSolo( trackName, true, soloResult, error ) )
		{
			return tr( "Soloed track %1" ).arg( soloResult.value( "track_name" ).toString( trackName ) );
		}
		return error;
	}

	if( first == "unsolo" )
	{
		const QString trackName = ( tokens.size() >= 2 && tokens[1].compare( "track", Qt::CaseInsensitive ) == 0 ) ?
			joinTokens( tokens, 2 ) : joinTokens( tokens, 1 );
		QJsonObject soloResult;
		if( setTrackSolo( trackName, false, soloResult, error ) )
		{
			return tr( "Unsoloed track %1" ).arg( soloResult.value( "track_name" ).toString( trackName ) );
		}
		return error;
	}

	if( first == "undo" )
	{
		QJsonObject undoResult;
		if( undoLastAction( undoResult, error ) )
		{
			return tr( "Undid last action" );
		}
		return error;
	}

	return tr( "Unknown command: %1. For natural-language tasks use lmmsagent/scripts/run_text_agent.sh" ).arg( rawText );
}

QJsonObject AgentControlService::dispatchTool( const QString& toolName, const QJsonObject& args )
{
	const QString tool = normalizeName( toolName );
	const QSet<QString> writeTools =
	{
		"createtrack", "renametrack", "loadinstrument", "loadsample", "createpattern",
		"addnotes", "addsteps", "settempo", "addeffect", "removeeffect", "seteffectparam",
		"opentool", "importaudio", "importmidi", "importhydrogen", "selecttrack", "mutetrack",
		"solotrack", "undoslastaction", "undolastaction", "createsnapshot", "rollbacktosnapshot"
	};
	const QSet<QString> mutatingTools =
	{
		"createtrack", "renametrack", "loadinstrument", "loadsample", "createpattern",
		"addnotes", "addsteps", "settempo", "addeffect", "removeeffect", "seteffectparam",
		"importaudio", "importmidi", "importhydrogen", "mutetrack", "solotrack"
	};

	const bool isWriteTool = writeTools.contains( tool );
	const QJsonObject beforeState = isWriteTool ? projectStateObject() : QJsonObject();
	QJsonObject result;
	QString error;
	bool ok = true;
	QJsonArray warnings;

	auto isKnownWindow = [this]( const QString& name )
	{
		const QString wanted = normalizeName( name );
		for( const auto &value : availableWindows() )
		{
			if( normalizeName( value.toString() ) == wanted )
			{
				return true;
			}
		}
		return false;
	};

	if( tool == "getprojectstate" )
	{
		result = projectStateObject();
	}
	else if( tool == "listtracks" )
	{
		result = QJsonObject{ { "tracks", trackArray() } };
	}
	else if( tool == "gettrackdetails" )
	{
		Track* track = resolveTrackRef( args );
		if( track == nullptr )
		{
			return errorResponse( "track_not_found", tr( "Track not found" ) );
		}
		result = trackObject( track, trackIndex( track ) );
	}
	else if( tool == "listpatterns" )
	{
		result = QJsonObject{ { "patterns", listPatternArray() } };
	}
	else if( tool == "listinstruments" )
	{
		QJsonArray activeTracks;
		Song* song = Engine::getSong();
		if( song != nullptr )
		{
			int idx = 0;
			for( const auto *track : song->tracks() )
			{
				if( dynamic_cast<const InstrumentTrack *>( track ) != nullptr )
				{
					activeTracks.append( trackObject( track, idx ) );
				}
				++idx;
			}
		}
		result = QJsonObject
		{
			{ "installed", availableInstruments() },
			{ "active_tracks", activeTracks }
		};
	}
	else if( tool == "listeffects" )
	{
		Track* track = resolveTrackRef( args );
		result = QJsonObject
		{
			{ "installed", availableEffects() },
			{ "track_effects", track != nullptr ? effectArrayForTrack( track ) : QJsonArray() }
		};
	}
	else if( tool == "listtoolwindows" )
	{
		result = QJsonObject
		{
			{ "windows", availableWindows() },
			{ "tools", availableTools() }
		};
	}
	else if( tool == "getselectionstate" )
	{
		result = QJsonObject
		{
			{ "selected_track", m_selectedTrackName },
			{ "has_selection", !m_selectedTrackName.isEmpty() }
		};
	}
	else if( tool == "findtrackbyname" )
	{
		const QString query = args.value( "query" ).toString().trimmed().isEmpty()
			? args.value( "name" ).toString().trimmed()
			: args.value( "query" ).toString().trimmed();
		if( query.isEmpty() )
		{
			return errorResponse( "invalid_args", tr( "Missing query for find_track_by_name" ) );
		}
		const QString wanted = normalizeName( query );
		Song* song = Engine::getSong();
		if( song == nullptr )
		{
			return errorResponse( "no_song", tr( "No active song" ) );
		}
		const auto &tracks = song->tracks();
		Track* best = nullptr;
		for( auto *track : tracks )
		{
			const QString trackName = normalizeName( track->name() );
			if( trackName == wanted )
			{
				best = track;
				break;
			}
			if( best == nullptr && trackName.contains( wanted ) )
			{
				best = track;
			}
		}
		if( best == nullptr )
		{
			return errorResponse( "track_not_found", tr( "No track matched %1" ).arg( query ) );
		}
		result = QJsonObject{ { "match", trackObject( best, trackIndex( best ) ) } };
	}
	else if( tool == "searchprojectaudio" )
	{
		const QString query = args.value( "query" ).toString().trimmed();
		result = QJsonObject{ { "matches", searchProjectAudio( query ) } };
	}
	else if( tool == "createtrack" )
	{
		const QString typeName = normalizeName( args.value( "type" ).toString().trimmed() );
		Track::Type type = Track::Type::Instrument;
		if( typeName == "sample" || typeName == "sampletrack" )
		{
			type = Track::Type::Sample;
		}
		else if( typeName == "automation" || typeName == "automationtrack" )
		{
			type = Track::Type::Automation;
		}
		else if( typeName == "pattern" || typeName == "patterntrack" )
		{
			type = Track::Type::Pattern;
		}
		Song* song = Engine::getSong();
		if( song == nullptr )
		{
			return errorResponse( "no_song", tr( "No active song" ) );
		}
		Track* track = Track::create( type, song );
		if( track == nullptr )
		{
			return errorResponse( "create_failed", tr( "Failed to create track" ) );
		}
		const QString name = args.value( "name" ).toString().trimmed();
		if( !name.isEmpty() )
		{
			track->setName( name );
		}
		m_selectedTrackName = track->name();
		result = QJsonObject{ { "track", trackObject( track, trackIndex( track ) ) } };
	}
	else if( tool == "renametrack" )
	{
		ok = renameTrack(
			args.value( "track" ).toString().trimmed().isEmpty()
				? args.value( "track_name" ).toString().trimmed()
				: args.value( "track" ).toString().trimmed(),
			args.value( "new_name" ).toString().trimmed(),
			result, error );
	}
	else if( tool == "loadinstrument" )
	{
		const QString pluginQuery = args.value( "plugin" ).toString().trimmed().isEmpty()
			? args.value( "name" ).toString().trimmed()
			: args.value( "plugin" ).toString().trimmed();
		if( pluginQuery.isEmpty() )
		{
			return errorResponse( "invalid_args", tr( "Missing plugin for load_instrument" ) );
		}

		QString displayName;
		const QString pluginName = resolveInstrumentPlugin( pluginQuery, displayName );
		if( pluginName.isEmpty() )
		{
			return errorResponse( "plugin_not_found", tr( "Unknown instrument: %1" ).arg( pluginQuery ) );
		}

		InstrumentTrack* track = resolveInstrumentTrack( args );
		if( track == nullptr )
		{
			Song* song = Engine::getSong();
			if( song == nullptr )
			{
				return errorResponse( "no_song", tr( "No active song" ) );
			}
			track = dynamic_cast<InstrumentTrack *>( Track::create( Track::Type::Instrument, song ) );
		}
		if( track == nullptr )
		{
			return errorResponse( "create_failed", tr( "Could not create instrument track" ) );
		}
		track->loadInstrument( pluginName );
		if( !args.value( "track_name" ).toString().trimmed().isEmpty() )
		{
			track->setName( args.value( "track_name" ).toString().trimmed() );
		}
		m_selectedTrackName = track->name();
		result = QJsonObject
		{
			{ "plugin", pluginName },
			{ "plugin_display_name", displayName },
			{ "track", trackObject( track, trackIndex( track ) ) }
		};
	}
	else if( tool == "loadsample" )
	{
		const QString samplePath = args.value( "sample_path" ).toString().trimmed().isEmpty()
			? args.value( "path" ).toString().trimmed()
			: args.value( "sample_path" ).toString().trimmed();
		const QString trackName = args.value( "track" ).toString().trimmed().isEmpty()
			? args.value( "track_name" ).toString().trimmed()
			: args.value( "track" ).toString().trimmed();
		ok = loadSampleToTrack( samplePath, trackName, result, error );
	}
	else if( tool == "createpattern" )
	{
		ok = createPatternClip( args, result, error );
	}
	else if( tool == "addnotes" )
	{
		ok = addNotesToPattern( args, result, error );
	}
	else if( tool == "addsteps" )
	{
		ok = addStepsToPattern( args, result, error );
	}
	else if( tool == "settempo" )
	{
		const int tempo = args.value( "tempo" ).toInt( 0 );
		ok = setTempoValue( tempo, result, error );
	}
	else if( tool == "addeffect" )
	{
		QString message;
		const QString effectName = args.value( "effect" ).toString().trimmed().isEmpty()
			? args.value( "name" ).toString().trimmed()
			: args.value( "effect" ).toString().trimmed();
		const QString trackName = args.value( "track" ).toString().trimmed();
		ok = addEffectToTrack( effectName, trackName, message, error );
		result = QJsonObject{ { "message", message } };
	}
	else if( tool == "removeeffect" )
	{
		QString message;
		const QString effectName = args.value( "effect" ).toString().trimmed().isEmpty()
			? args.value( "name" ).toString().trimmed()
			: args.value( "effect" ).toString().trimmed();
		const QString trackName = args.value( "track" ).toString().trimmed();
		ok = removeEffectFromTrack( effectName, trackName, message, error );
		result = QJsonObject{ { "message", message } };
	}
	else if( tool == "seteffectparam" )
	{
		return errorResponse( "not_implemented", tr( "set_effect_param is not implemented yet" ) );
	}
	else if( tool == "opentool" )
	{
		QString message;
		const QString name = args.value( "name" ).toString().trimmed();
		if( name.isEmpty() )
		{
			return errorResponse( "invalid_args", tr( "Missing tool or window name" ) );
		}
		const QString kind = normalizeName( args.value( "kind" ).toString().trimmed() );
		if( kind == "window" )
		{
			ok = showWindowCommand( name, message, error );
		}
		else if( kind == "tool" )
		{
			ok = showToolCommand( name, message, error );
		}
		else if( isKnownWindow( name ) )
		{
			ok = showWindowCommand( name, message, error );
		}
		else
		{
			ok = showToolCommand( name, message, error );
			if( !ok )
			{
				ok = showWindowCommand( name, message, error );
			}
		}
		result = QJsonObject{ { "message", message } };
	}
	else if( tool == "importaudio" )
	{
		const QString path = args.value( "path" ).toString().trimmed();
		ok = importAudioFile( path, error );
		result = QJsonObject{ { "path", path } };
	}
	else if( tool == "importmidi" )
	{
		const QString path = args.value( "path" ).toString().trimmed();
		ok = importProjectFile( path, error );
		result = QJsonObject{ { "path", path }, { "format", "midi" } };
	}
	else if( tool == "importhydrogen" )
	{
		const QString path = args.value( "path" ).toString().trimmed();
		ok = importProjectFile( path, error );
		result = QJsonObject{ { "path", path }, { "format", "hydrogen" } };
	}
	else if( tool == "selecttrack" )
	{
		const QString trackName = args.value( "track" ).toString().trimmed().isEmpty()
			? args.value( "track_name" ).toString().trimmed()
			: args.value( "track" ).toString().trimmed();
		ok = selectTrack( trackName, result, error );
	}
	else if( tool == "mutetrack" )
	{
		const QString trackName = args.value( "track" ).toString().trimmed().isEmpty()
			? args.value( "track_name" ).toString().trimmed()
			: args.value( "track" ).toString().trimmed();
		const bool mute = args.value( "mute" ).toBool( true );
		ok = setTrackMute( trackName, mute, result, error );
	}
	else if( tool == "solotrack" )
	{
		const QString trackName = args.value( "track" ).toString().trimmed().isEmpty()
			? args.value( "track_name" ).toString().trimmed()
			: args.value( "track" ).toString().trimmed();
		const bool solo = args.value( "solo" ).toBool( true );
		ok = setTrackSolo( trackName, solo, result, error );
	}
	else if( tool == "createsnapshot" )
	{
		ok = createSnapshot( args.value( "label" ).toString().trimmed(), result, error );
	}
	else if( tool == "undolastaction" || tool == "undoslastaction" )
	{
		ok = undoLastAction( result, error );
	}
	else if( tool == "rollbacktosnapshot" )
	{
		const QString snapshotId = args.value( "snapshot_id" ).toString().trimmed();
		ok = rollbackSnapshot( snapshotId, result, error );
	}
	else if( tool == "diffsincesnapshot" || tool == "diffsincesnapshop" || tool == "diffsince_snapshot" )
	{
		const QString snapshotId = args.value( "snapshot_id" ).toString().trimmed();
		ok = diffSinceSnapshot( snapshotId, result, error );
	}
	else
	{
		return errorResponse( "unknown_tool", tr( "Unknown tool: %1" ).arg( toolName ) );
	}

	if( !ok )
	{
		return errorResponse( "tool_failed", error.isEmpty() ? tr( "Tool execution failed" ) : error, warnings );
	}

	QJsonObject stateDelta;
	if( isWriteTool )
	{
		const QJsonObject afterState = projectStateObject();
		stateDelta = diffState( beforeState, afterState );
		if( mutatingTools.contains( tool ) )
		{
			++m_actionCounter;
		}
	}

	return successResponse( result, stateDelta, warnings );
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

	QByteArray& buffer = m_readBuffers[sock];
	buffer.append( sock->readAll() );

	int newlineIndex = buffer.indexOf( '\n' );
	while( newlineIndex >= 0 )
	{
		const QByteArray line = buffer.left( newlineIndex );
		buffer.remove( 0, newlineIndex + 1 );
		newlineIndex = buffer.indexOf( '\n' );

		if( line.trimmed().isEmpty() )
		{
			continue;
		}

		QJsonParseError parseError {};
		const QJsonDocument doc = QJsonDocument::fromJson( line, &parseError );
		QJsonObject reply;
		QString displayResult;
		if( parseError.error == QJsonParseError::NoError && doc.isObject() )
		{
			reply = handleRequest( doc.object() );
			displayResult = toCommandResponseText( reply );
		}
		else
		{
			const QString result = handleCommand( QString::fromUtf8( line ) );
			displayResult = result;
			reply = successResponse( QJsonObject
			{
				{ "message", result }
			} );
		}

		if( !reply.contains( "ok" ) )
		{
			reply = errorResponse( "invalid_response", tr( "AgentControl returned an invalid response" ) );
		}

		const bool ok = reply.value( "ok" ).toBool( false );
		if( !ok && reply.value( "error_message" ).toString().isEmpty() )
		{
			reply["error_message"] = tr( "Request failed" );
		}
		if( !ok )
		{
			displayResult = reply.value( "error_message" ).toString();
		}
		emit commandResult( displayResult );
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
	m_readBuffers.remove( sock );
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
	if( m_projectTransitionQueued )
	{
		error = tr( "A project open/new operation is already in progress" );
		return false;
	}
	m_projectTransitionQueued = true;
	QTimer::singleShot( 0, guiApp->mainWindow(), [this, song]()
	{
		song->createNewProject();
		m_projectTransitionQueued = false;
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
	if( m_projectTransitionQueued )
	{
		error = tr( "A project open/new operation is already in progress" );
		return false;
	}
	m_projectTransitionQueued = true;
	QTimer::singleShot( 0, guiApp->mainWindow(), [this, song, fullPath]()
	{
		song->loadProject( fullPath );
		m_projectTransitionQueued = false;
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

	QString normalized = normalizeName( windowName );
	// Accept common feature labels and human aliases used in docs/manual text.
	if( normalized == "fxmixer" || normalized == "fxmixereffects" ||
		normalized == "mixereffects" || normalized == "effectsmixer" ||
		normalized == "effectchain" || normalized == "effects" )
	{
		normalized = "mixer";
	}
	else if( normalized == "songeditortracks" || normalized == "songarrangement" )
	{
		normalized = "songeditor";
	}
	else if( normalized == "pianorolleditor" || normalized == "melodyeditor" )
	{
		normalized = "pianoroll";
	}
	else if( normalized == "automationeditorandcontrollerrack" || normalized == "automationcontrollers" )
	{
		normalized = "automationeditor";
	}

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

bool AgentControlService::addSnarePattern( QString& error )
{
	const QString samplePath = defaultSnareSample();
	if( samplePath.isEmpty() )
	{
		error = tr( "Snare sample missing" );
		return false;
	}

	auto *track = createSampleTrack( tr( "Agent Snare" ) );
	if( track == nullptr )
	{
		error = tr( "Failed to create sample track" );
		return false;
	}

	const int stepTicks = DefaultTicksPerBar / DefaultStepsPerBar;
	const int steps[] = { 4, 12 };
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

QString AgentControlService::defaultSnareSample() const
{
	const QString base = ConfigManager::inst()->factorySamplesDir();
	const QString preferred = QDir( base ).filePath( "drums/snare01.ogg" );
	if( QFile::exists( preferred ) )
	{
		return preferred;
	}
	const QString fallback = QDir( base ).filePath( "drums/snare03.ogg" );
	return QFile::exists( fallback ) ? fallback : QString{};
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

QJsonObject AgentControlService::successResponse(
	const QJsonObject& result,
	const QJsonObject& stateDelta,
	const QJsonArray& warnings ) const
{
	return QJsonObject
	{
		{ "ok", true },
		{ "result", result },
		{ "state_delta", stateDelta },
		{ "warnings", warnings },
		{ "error_code", QJsonValue::Null },
		{ "error_message", QJsonValue::Null }
	};
}

QJsonObject AgentControlService::errorResponse(
	const QString& errorCode,
	const QString& errorMessage,
	const QJsonArray& warnings ) const
{
	return QJsonObject
	{
		{ "ok", false },
		{ "result", QJsonObject() },
		{ "state_delta", QJsonObject() },
		{ "warnings", warnings },
		{ "error_code", errorCode },
		{ "error_message", errorMessage }
	};
}

QString AgentControlService::toCommandResponseText( const QJsonObject& response ) const
{
	if( !response.value( "ok" ).toBool( false ) )
	{
		return response.value( "error_message" ).toString( tr( "Request failed" ) );
	}

	const QJsonObject result = response.value( "result" ).toObject();
	const QString message = result.value( "message" ).toString();
	if( !message.isEmpty() )
	{
		return message;
	}
	return QString::fromUtf8( QJsonDocument( result ).toJson( QJsonDocument::Compact ) );
}

QString AgentControlService::trackTypeName( Track::Type type ) const
{
	switch( type )
	{
	case Track::Type::Instrument:
		return "instrument";
	case Track::Type::Pattern:
		return "pattern";
	case Track::Type::Sample:
		return "sample";
	case Track::Type::Event:
		return "event";
	case Track::Type::Video:
		return "video";
	case Track::Type::Automation:
		return "automation";
	case Track::Type::HiddenAutomation:
		return "hidden_automation";
	case Track::Type::Count:
	default:
		return "unknown";
	}
}

int AgentControlService::trackIndex( const Track* track ) const
{
	Song* song = Engine::getSong();
	if( song == nullptr )
	{
		return -1;
	}
	const auto &tracks = song->tracks();
	for( std::size_t i = 0; i < tracks.size(); ++i )
	{
		if( tracks[i] == track )
		{
			return static_cast<int>( i );
		}
	}
	return -1;
}

QJsonArray AgentControlService::effectArrayForTrack( Track* track ) const
{
	QJsonArray effects;
	EffectChain* chain = effectChainForTrack( track );
	if( chain == nullptr )
	{
		return effects;
	}

	for( Effect* effect : chain->effects() )
	{
		QJsonObject item
		{
			{ "display_name", effect->displayName() }
		};
		if( effect->descriptor() != nullptr )
		{
			item.insert( "plugin_name", QString::fromUtf8( effect->descriptor()->name ) );
		}
		effects.append( item );
	}
	return effects;
}

QJsonObject AgentControlService::trackObject( const Track* track, int index ) const
{
	QJsonObject obj
	{
		{ "id", QString( "t_%1" ).arg( index ) },
		{ "index", index },
		{ "name", track->name() },
		{ "type", trackTypeName( track->type() ) },
		{ "muted", track->isMuted() },
		{ "solo", track->isSolo() },
		{ "clip_count", static_cast<int>( track->getClips().size() ) },
		{ "effects", effectArrayForTrack( const_cast<Track *>( track ) ) }
	};

	if( const auto *instrumentTrack = dynamic_cast<const InstrumentTrack *>( track ) )
	{
		obj.insert( "instrument_name", instrumentTrack->instrumentName() );
	}
	if( const auto *sampleTrack = dynamic_cast<const SampleTrack *>( track ) )
	{
		QJsonArray sampleFiles;
		for( auto *clip : sampleTrack->getClips() )
		{
			if( auto *sampleClip = dynamic_cast<SampleClip *>( clip ) )
			{
				sampleFiles.append( sampleClip->sampleFile() );
			}
		}
		obj.insert( "sample_files", sampleFiles );
	}
	return obj;
}

QJsonArray AgentControlService::trackArray() const
{
	QJsonArray tracksJson;
	Song* song = Engine::getSong();
	if( song == nullptr )
	{
		return tracksJson;
	}

	int index = 0;
	for( const auto *track : song->tracks() )
	{
		tracksJson.append( trackObject( track, index ) );
		++index;
	}
	return tracksJson;
}

QJsonArray AgentControlService::listPatternArray() const
{
	QJsonArray patterns;
	Song* song = Engine::getSong();
	if( song == nullptr )
	{
		return patterns;
	}

	int trackIdx = 0;
	for( auto *track : song->tracks() )
	{
		int clipIdx = 0;
		for( auto *clip : track->getClips() )
		{
			if( auto *midiClip = dynamic_cast<MidiClip *>( clip ) )
			{
				patterns.append( QJsonObject
				{
					{ "track_id", QString( "t_%1" ).arg( trackIdx ) },
					{ "track_name", track->name() },
					{ "clip_index", clipIdx },
					{ "clip_name", midiClip->name() },
					{ "start_tick", static_cast<int>( midiClip->startPosition() ) },
					{ "length_ticks", static_cast<int>( midiClip->length() ) },
					{ "note_count", static_cast<int>( midiClip->notes().size() ) }
				} );
			}
			++clipIdx;
		}
		++trackIdx;
	}
	return patterns;
}

QJsonObject AgentControlService::projectStateObject() const
{
	QJsonObject state;
	Song* song = Engine::getSong();
	if( song == nullptr )
	{
		return state;
	}

	state.insert( "project_file", song->projectFileName() );
	state.insert( "tempo", song->getTempo() );
	state.insert( "track_count", static_cast<int>( song->tracks().size() ) );
	state.insert( "selected_track", m_selectedTrackName );
	state.insert( "tracks", trackArray() );
	return state;
}

QJsonObject AgentControlService::diffState( const QJsonObject& before, const QJsonObject& after ) const
{
	QJsonObject diff;
	diff.insert( "track_count_before", before.value( "track_count" ).toInt() );
	diff.insert( "track_count_after", after.value( "track_count" ).toInt() );
	diff.insert( "tempo_before", before.value( "tempo" ).toInt() );
	diff.insert( "tempo_after", after.value( "tempo" ).toInt() );

	QSet<QString> beforeTracks;
	QSet<QString> afterTracks;
	for( const auto &value : before.value( "tracks" ).toArray() )
	{
		beforeTracks.insert( value.toObject().value( "name" ).toString() );
	}
	for( const auto &value : after.value( "tracks" ).toArray() )
	{
		afterTracks.insert( value.toObject().value( "name" ).toString() );
	}

	int added = 0;
	for( const auto &name : afterTracks )
	{
		if( !beforeTracks.contains( name ) )
		{
			++added;
		}
	}

	int removed = 0;
	for( const auto &name : beforeTracks )
	{
		if( !afterTracks.contains( name ) )
		{
			++removed;
		}
	}

	diff.insert( "tracks_added", added );
	diff.insert( "tracks_removed", removed );
	diff.insert( "selected_track_before", before.value( "selected_track" ).toString() );
	diff.insert( "selected_track_after", after.value( "selected_track" ).toString() );
	return diff;
}

Track* AgentControlService::resolveTrackRef( const QJsonObject& args ) const
{
	Song* song = Engine::getSong();
	if( song == nullptr )
	{
		return nullptr;
	}

	const QString trackId = args.value( "track_id" ).toString().trimmed();
	if( !trackId.isEmpty() )
	{
		const QRegularExpression re( "^t_(\\d+)$" );
		const auto match = re.match( trackId );
		if( match.hasMatch() )
		{
			const int idx = match.captured( 1 ).toInt();
			if( idx >= 0 && idx < static_cast<int>( song->tracks().size() ) )
			{
				return song->tracks()[idx];
			}
		}
	}

	QString trackName = args.value( "track" ).toString().trimmed();
	if( trackName.isEmpty() )
	{
		trackName = args.value( "track_name" ).toString().trimmed();
	}
	if( trackName.isEmpty() )
	{
		trackName = m_selectedTrackName;
	}
	if( trackName.isEmpty() )
	{
		return nullptr;
	}

	if( auto *exact = findTrackByName( trackName ) )
	{
		return exact;
	}

	const QString wanted = normalizeName( trackName );
	for( auto *track : song->tracks() )
	{
		if( normalizeName( track->name() ).contains( wanted ) )
		{
			return track;
		}
	}
	return nullptr;
}

InstrumentTrack* AgentControlService::resolveInstrumentTrack( const QJsonObject& args ) const
{
	return dynamic_cast<InstrumentTrack *>( resolveTrackRef( args ) );
}

SampleTrack* AgentControlService::resolveSampleTrack( const QJsonObject& args ) const
{
	return dynamic_cast<SampleTrack *>( resolveTrackRef( args ) );
}

QString AgentControlService::resolveInstrumentPlugin( const QString& pluginName, QString& displayName ) const
{
	const QString wanted = normalizeName( pluginName );
	QString fuzzyMatch;
	QString fuzzyDisplay;

	for( const Plugin::Descriptor* desc : getPluginFactory()->descriptors( Plugin::Type::Instrument ) )
	{
		const QString byName = normalizeName( QString::fromUtf8( desc->name ) );
		const QString byDisplay = normalizeName( QString::fromUtf8( desc->displayName ) );
		if( byName == wanted || byDisplay == wanted )
		{
			displayName = QString::fromUtf8( desc->displayName );
			return QString::fromUtf8( desc->name );
		}
		if( fuzzyMatch.isEmpty() && ( byName.contains( wanted ) || byDisplay.contains( wanted ) ) )
		{
			fuzzyMatch = QString::fromUtf8( desc->name );
			fuzzyDisplay = QString::fromUtf8( desc->displayName );
		}
	}

	displayName = fuzzyDisplay;
	return fuzzyMatch;
}

QString AgentControlService::resolveEffectPlugin( const QString& effectName, QString& displayName ) const
{
	const QString wanted = normalizeName( effectName );
	QString fuzzyMatch;
	QString fuzzyDisplay;

	for( const Plugin::Descriptor* desc : getPluginFactory()->descriptors( Plugin::Type::Effect ) )
	{
		const QString byName = normalizeName( QString::fromUtf8( desc->name ) );
		const QString byDisplay = normalizeName( QString::fromUtf8( desc->displayName ) );
		if( byName == wanted || byDisplay == wanted )
		{
			displayName = QString::fromUtf8( desc->displayName );
			return QString::fromUtf8( desc->name );
		}
		if( fuzzyMatch.isEmpty() && ( byName.contains( wanted ) || byDisplay.contains( wanted ) ) )
		{
			fuzzyMatch = QString::fromUtf8( desc->name );
			fuzzyDisplay = QString::fromUtf8( desc->displayName );
		}
	}

	displayName = fuzzyDisplay;
	return fuzzyMatch;
}

QJsonArray AgentControlService::availableWindows() const
{
	return QJsonArray
	{
		"song editor",
		"pattern editor",
		"piano roll",
		"automation editor",
		"mixer",
		"controller rack",
		"project notes",
		"microtuner"
	};
}

QJsonArray AgentControlService::availableTools() const
{
	QJsonArray tools;
	for( const Plugin::Descriptor* desc : getPluginFactory()->descriptors( Plugin::Type::Tool ) )
	{
		tools.append( QJsonObject
		{
			{ "name", QString::fromUtf8( desc->name ) },
			{ "display_name", QString::fromUtf8( desc->displayName ) }
		} );
	}
	return tools;
}

QJsonArray AgentControlService::availableInstruments() const
{
	QJsonArray instruments;
	for( const Plugin::Descriptor* desc : getPluginFactory()->descriptors( Plugin::Type::Instrument ) )
	{
		instruments.append( QJsonObject
		{
			{ "name", QString::fromUtf8( desc->name ) },
			{ "display_name", QString::fromUtf8( desc->displayName ) }
		} );
	}
	return instruments;
}

QJsonArray AgentControlService::availableEffects() const
{
	QJsonArray effects;
	for( const Plugin::Descriptor* desc : getPluginFactory()->descriptors( Plugin::Type::Effect ) )
	{
		effects.append( QJsonObject
		{
			{ "name", QString::fromUtf8( desc->name ) },
			{ "display_name", QString::fromUtf8( desc->displayName ) }
		} );
	}
	return effects;
}

QJsonArray AgentControlService::searchProjectAudio( const QString& query ) const
{
	QJsonArray matches;
	Song* song = Engine::getSong();
	if( song == nullptr )
	{
		return matches;
	}

	const QString wanted = query.trimmed().toLower();
	for( auto *track : song->tracks() )
	{
		auto *sampleTrack = dynamic_cast<SampleTrack *>( track );
		if( sampleTrack == nullptr )
		{
			continue;
		}
		int clipIdx = 0;
		for( auto *clip : sampleTrack->getClips() )
		{
			auto *sampleClip = dynamic_cast<SampleClip *>( clip );
			if( sampleClip == nullptr )
			{
				++clipIdx;
				continue;
			}
			const QString path = sampleClip->sampleFile();
			if( wanted.isEmpty() || path.toLower().contains( wanted ) || QFileInfo( path ).fileName().toLower().contains( wanted ) )
			{
				matches.append( QJsonObject
				{
					{ "track_name", track->name() },
					{ "clip_index", clipIdx },
					{ "sample_path", path }
				} );
			}
			++clipIdx;
		}
	}
	return matches;
}

bool AgentControlService::createSnapshot( const QString& label, QJsonObject& result, QString& error )
{
	if( Engine::getSong() == nullptr )
	{
		error = tr( "No active song" );
		return false;
	}

	Snapshot snapshot;
	snapshot.id = QString( "snap_%1" ).arg( ++m_snapshotCounter );
	snapshot.label = label.isEmpty() ? snapshot.id : label;
	snapshot.state = projectStateObject();
	snapshot.actionCounter = m_actionCounter;
	m_snapshots.insert( snapshot.id, snapshot );

	result = QJsonObject
	{
		{ "snapshot_id", snapshot.id },
		{ "label", snapshot.label },
		{ "action_counter", snapshot.actionCounter }
	};
	return true;
}

bool AgentControlService::undoLastAction( QJsonObject& result, QString& error )
{
	ProjectJournal* journal = Engine::projectJournal();
	if( journal == nullptr || !journal->canUndo() )
	{
		error = tr( "Nothing to undo" );
		return false;
	}

	journal->undo();
	if( m_actionCounter > 0 )
	{
		--m_actionCounter;
	}
	result = QJsonObject
	{
		{ "undone", true },
		{ "action_counter", m_actionCounter }
	};
	return true;
}

bool AgentControlService::rollbackSnapshot( const QString& snapshotId, QJsonObject& result, QString& error )
{
	if( snapshotId.isEmpty() || !m_snapshots.contains( snapshotId ) )
	{
		error = tr( "Snapshot not found: %1" ).arg( snapshotId );
		return false;
	}
	ProjectJournal* journal = Engine::projectJournal();
	if( journal == nullptr )
	{
		error = tr( "Project journal is unavailable" );
		return false;
	}

	const Snapshot snapshot = m_snapshots.value( snapshotId );
	int undosRequired = m_actionCounter - snapshot.actionCounter;
	if( undosRequired < 0 )
	{
		undosRequired = 0;
	}

	int applied = 0;
	while( undosRequired > 0 && journal->canUndo() )
	{
		journal->undo();
		--undosRequired;
		++applied;
	}

	m_actionCounter = snapshot.actionCounter + undosRequired;
	result = QJsonObject
	{
		{ "snapshot_id", snapshotId },
		{ "undos_applied", applied },
		{ "action_counter", m_actionCounter }
	};
	return true;
}

bool AgentControlService::diffSinceSnapshot( const QString& snapshotId, QJsonObject& result, QString& error ) const
{
	if( snapshotId.isEmpty() || !m_snapshots.contains( snapshotId ) )
	{
		error = tr( "Snapshot not found: %1" ).arg( snapshotId );
		return false;
	}

	const Snapshot snapshot = m_snapshots.value( snapshotId );
	result = QJsonObject
	{
		{ "snapshot_id", snapshotId },
		{ "diff", diffState( snapshot.state, projectStateObject() ) }
	};
	return true;
}

bool AgentControlService::loadSampleToTrack(
	const QString& samplePath,
	const QString& trackName,
	QJsonObject& result,
	QString& error )
{
	const QString fullPath = canonicalPath( samplePath );
	if( fullPath.isEmpty() )
	{
		error = tr( "File not found: %1" ).arg( samplePath );
		return false;
	}

	SampleTrack* track = nullptr;
	if( !trackName.isEmpty() )
	{
		track = dynamic_cast<SampleTrack *>( findTrackByName( trackName ) );
		if( track == nullptr )
		{
			track = createSampleTrack( trackName );
		}
	}
	else if( !m_selectedTrackName.isEmpty() )
	{
		track = dynamic_cast<SampleTrack *>( findTrackByName( m_selectedTrackName ) );
	}
	if( track == nullptr )
	{
		track = createSampleTrack( QFileInfo( fullPath ).baseName() );
	}
	if( track == nullptr )
	{
		error = tr( "Failed to create sample track" );
		return false;
	}

	if( !addSampleClip( track, fullPath, 0 ) )
	{
		error = tr( "Failed to add sample clip" );
		return false;
	}

	m_selectedTrackName = track->name();
	result = QJsonObject
	{
		{ "sample_path", fullPath },
		{ "track", trackObject( track, trackIndex( track ) ) }
	};
	return true;
}

bool AgentControlService::setTempoValue( int tempo, QJsonObject& result, QString& error )
{
	Song* song = Engine::getSong();
	if( song == nullptr )
	{
		error = tr( "No active song" );
		return false;
	}
	if( tempo < MinTempo || tempo > MaxTempo )
	{
		error = tr( "Tempo must be between %1 and %2" ).arg( MinTempo ).arg( MaxTempo );
		return false;
	}

	const int oldTempo = song->getTempo();
	song->setTempo( tempo );
	result = QJsonObject
	{
		{ "tempo_before", oldTempo },
		{ "tempo_after", song->getTempo() }
	};
	return true;
}

bool AgentControlService::renameTrack(
	const QString& trackName,
	const QString& newName,
	QJsonObject& result,
	QString& error )
{
	if( newName.isEmpty() )
	{
		error = tr( "new_name must not be empty" );
		return false;
	}

	Track* track = findTrackByName( trackName.isEmpty() ? m_selectedTrackName : trackName );
	if( track == nullptr )
	{
		error = tr( "Track not found: %1" ).arg( trackName );
		return false;
	}

	const QString oldName = track->name();
	track->setName( newName );
	m_selectedTrackName = track->name();
	result = QJsonObject
	{
		{ "old_name", oldName },
		{ "new_name", track->name() },
		{ "track", trackObject( track, trackIndex( track ) ) }
	};
	return true;
}

bool AgentControlService::selectTrack( const QString& trackName, QJsonObject& result, QString& error )
{
	Track* track = findTrackByName( trackName );
	if( track == nullptr )
	{
		error = tr( "Track not found: %1" ).arg( trackName );
		return false;
	}
	m_selectedTrackName = track->name();
	result = QJsonObject
	{
		{ "selected_track", track->name() },
		{ "track", trackObject( track, trackIndex( track ) ) }
	};
	return true;
}

bool AgentControlService::setTrackMute( const QString& trackName, bool mute, QJsonObject& result, QString& error )
{
	Track* track = findTrackByName( trackName.isEmpty() ? m_selectedTrackName : trackName );
	if( track == nullptr )
	{
		error = tr( "Track not found: %1" ).arg( trackName );
		return false;
	}
	track->setMuted( mute );
	result = QJsonObject
	{
		{ "track", track->name() },
		{ "muted", track->isMuted() }
	};
	return true;
}

bool AgentControlService::setTrackSolo( const QString& trackName, bool solo, QJsonObject& result, QString& error )
{
	Track* track = findTrackByName( trackName.isEmpty() ? m_selectedTrackName : trackName );
	if( track == nullptr )
	{
		error = tr( "Track not found: %1" ).arg( trackName );
		return false;
	}
	track->setSolo( solo );
	result = QJsonObject
	{
		{ "track", track->name() },
		{ "solo", track->isSolo() }
	};
	return true;
}

bool AgentControlService::createPatternClip( const QJsonObject& args, QJsonObject& result, QString& error )
{
	InstrumentTrack* track = resolveInstrumentTrack( args );
	if( track == nullptr )
	{
		track = dynamic_cast<InstrumentTrack *>( findLastTrackOfTypes( { Track::Type::Instrument } ) );
	}
	if( track == nullptr )
	{
		error = tr( "No instrument track available for create_pattern" );
		return false;
	}

	const int tick = args.value( "tick" ).toInt( 0 );
	Clip* clip = track->createClip( TimePos( tick ) );
	if( clip == nullptr )
	{
		error = tr( "Failed to create pattern clip" );
		return false;
	}
	const QString clipName = args.value( "name" ).toString().trimmed();
	if( !clipName.isEmpty() )
	{
		clip->setName( clipName );
	}

	m_selectedTrackName = track->name();
	result = QJsonObject
	{
		{ "track", track->name() },
		{ "clip_index", track->getClipNum( clip ) },
		{ "clip_name", clip->name() },
		{ "start_tick", tick }
	};
	return true;
}

bool AgentControlService::addNotesToPattern( const QJsonObject& args, QJsonObject& result, QString& error )
{
	InstrumentTrack* track = resolveInstrumentTrack( args );
	if( track == nullptr )
	{
		track = dynamic_cast<InstrumentTrack *>( findLastTrackOfTypes( { Track::Type::Instrument } ) );
	}
	if( track == nullptr )
	{
		error = tr( "No instrument track available for add_notes" );
		return false;
	}

	MidiClip* targetClip = nullptr;
	const int requestedClipIndex = args.value( "clip_index" ).toInt( -1 );
	if( requestedClipIndex >= 0 && requestedClipIndex < static_cast<int>( track->getClips().size() ) )
	{
		targetClip = dynamic_cast<MidiClip *>( track->getClip( static_cast<std::size_t>( requestedClipIndex ) ) );
	}
	if( targetClip == nullptr )
	{
		for( auto it = track->getClips().rbegin(); it != track->getClips().rend(); ++it )
		{
			targetClip = dynamic_cast<MidiClip *>( *it );
			if( targetClip != nullptr )
			{
				break;
			}
		}
	}
	if( targetClip == nullptr )
	{
		Clip* clip = track->createClip( TimePos( 0 ) );
		targetClip = dynamic_cast<MidiClip *>( clip );
	}
	if( targetClip == nullptr )
	{
		error = tr( "Failed to resolve target pattern clip" );
		return false;
	}

	QJsonArray notes = args.value( "notes" ).toArray();
	if( notes.isEmpty() && args.contains( "key" ) )
	{
		notes.append( QJsonObject
		{
			{ "key", args.value( "key" ).toInt( DefaultKey ) },
			{ "pos", args.value( "pos" ).toInt( 0 ) },
			{ "length", args.value( "length" ).toInt( DefaultTicksPerBar / 4 ) },
			{ "velocity", args.value( "velocity" ).toInt( 100 ) }
		} );
	}
	if( notes.isEmpty() )
	{
		error = tr( "add_notes requires notes array or key/pos/length fields" );
		return false;
	}

	int added = 0;
	for( const auto &entry : notes )
	{
		const QJsonObject noteObj = entry.toObject();
		const int key = noteObj.value( "key" ).toInt( DefaultKey );
		const int pos = noteObj.value( "pos" ).toInt( 0 );
		const int length = noteObj.value( "length" ).toInt( DefaultTicksPerBar / 4 );
		const int velocity = qBound( 1, noteObj.value( "velocity" ).toInt( 100 ), 200 );
		Note note( TimePos( length ), TimePos( pos ), key, velocity );
		targetClip->addNote( note, false );
		++added;
	}
	targetClip->updateLength();

	result = QJsonObject
	{
		{ "track", track->name() },
		{ "clip_index", track->getClipNum( targetClip ) },
		{ "notes_added", added }
	};
	return true;
}

bool AgentControlService::addStepsToPattern( const QJsonObject& args, QJsonObject& result, QString& error )
{
	InstrumentTrack* track = resolveInstrumentTrack( args );
	if( track == nullptr )
	{
		track = dynamic_cast<InstrumentTrack *>( findLastTrackOfTypes( { Track::Type::Instrument } ) );
	}
	if( track == nullptr )
	{
		error = tr( "No instrument track available for add_steps" );
		return false;
	}

	MidiClip* targetClip = nullptr;
	const int requestedClipIndex = args.value( "clip_index" ).toInt( -1 );
	if( requestedClipIndex >= 0 && requestedClipIndex < static_cast<int>( track->getClips().size() ) )
	{
		targetClip = dynamic_cast<MidiClip *>( track->getClip( static_cast<std::size_t>( requestedClipIndex ) ) );
	}
	if( targetClip == nullptr )
	{
		for( auto it = track->getClips().rbegin(); it != track->getClips().rend(); ++it )
		{
			targetClip = dynamic_cast<MidiClip *>( *it );
			if( targetClip != nullptr )
			{
				break;
			}
		}
	}
	if( targetClip == nullptr )
	{
		Clip* clip = track->createClip( TimePos( 0 ) );
		targetClip = dynamic_cast<MidiClip *>( clip );
	}
	if( targetClip == nullptr )
	{
		error = tr( "Failed to resolve target pattern clip" );
		return false;
	}

	if( args.value( "clear_existing" ).toBool( false ) )
	{
		targetClip->clearNotes();
	}

	QJsonArray steps = args.value( "steps" ).toArray();
	if( steps.isEmpty() )
	{
		steps = QJsonArray{ 0, 4, 8, 12 };
	}

	int added = 0;
	for( const auto &stepValue : steps )
	{
		targetClip->setStep( stepValue.toInt(), true );
		++added;
	}

	result = QJsonObject
	{
		{ "track", track->name() },
		{ "clip_index", track->getClipNum( targetClip ) },
		{ "steps_enabled", added }
	};
	return true;
}

} // namespace lmms
