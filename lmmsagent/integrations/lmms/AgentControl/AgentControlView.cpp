#include "AgentControlView.h"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QHttpMultiPart>
#include <QHBoxLayout>
#include <QVBoxLayout>
#include <QEventLoop>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QPushButton>
#include <QStandardPaths>
#include <QThread>
#include <QTimer>
#include <QUrl>

#include "AgentControl.h"

namespace lmms::gui
{

AgentControlView::AgentControlView( AgentControlPlugin *plugin )
	: ToolPluginView( plugin )
	, m_plugin( plugin )
	, m_input( new QLineEdit( this ) )
	, m_log( new QTextEdit( this ) )
	, m_status( new QLabel( tr( "Listening on 127.0.0.1:7777" ), this ) )
		, m_voiceStartButton( new QPushButton( tr( "Start Voice" ), this ) )
		, m_voiceStopButton( new QPushButton( tr( "Stop + Run" ), this ) )
		, m_voiceRecordProcess( nullptr )
		, m_whisperServiceProcess( nullptr )
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
	auto voiceRow = new QHBoxLayout();
	voiceRow->addWidget( m_voiceStartButton );
	voiceRow->addWidget( m_voiceStopButton );
	layout->addLayout( voiceRow );
	layout->addWidget( m_log );

	connect( runBtn, &QPushButton::clicked, this, &AgentControlView::runCommand );
	connect( m_input, &QLineEdit::returnPressed, this, &AgentControlView::runCommand );
	connect( m_voiceStartButton, &QPushButton::clicked, this, &AgentControlView::startVoiceCapture );
	connect( m_voiceStopButton, &QPushButton::clicked, this, &AgentControlView::stopVoiceCapture );
	connect( m_plugin, &AgentControlPlugin::logMessage, this, &AgentControlView::appendLog );
	connect( m_plugin, &AgentControlPlugin::commandResult, this, &AgentControlView::appendLog );
	setVoiceUiState( false );
	appendLog( tr( "Voice: ffmpeg=%1 whisper=%2 mic=%3" )
		.arg( ffmpegPath() )
		.arg( whisperPath() )
		.arg( microphoneDevice() ) );
	if( QFileInfo( whisperPath() ).fileName().toLower() == QStringLiteral( "whisper-cli" ) )
	{
		const QString modelPath = whisperModelPath();
		appendLog( tr( "Whisper model: %1" )
			.arg( modelPath.isEmpty() ? tr( "not found" ) : modelPath ) );
	}
	if( useWhisperService() )
	{
		appendLog( tr( "Whisper service mode enabled: %1 (model=%2)" )
			.arg( whisperServiceBaseUrl(), whisperServiceModel() ) );
	}

	hide();
	if( parentWidget() )
	{
		parentWidget()->hide();
	}
}

AgentControlView::~AgentControlView()
{
	if( m_voiceRecordProcess != nullptr )
	{
		if( m_voiceRecordProcess->state() != QProcess::NotRunning )
		{
			m_voiceRecordProcess->terminate();
			if( !m_voiceRecordProcess->waitForFinished( 500 ) )
			{
				m_voiceRecordProcess->kill();
				m_voiceRecordProcess->waitForFinished( 500 );
			}
		}
		m_voiceRecordProcess->deleteLater();
		m_voiceRecordProcess = nullptr;
	}
	shutdownWhisperService();
}

void AgentControlView::runCommand()
{
	const QString text = m_input->text().trimmed();
	if( text.isEmpty() )
	{
		appendLog( tr( "No command entered" ) );
		return;
	}
	const QString result = m_plugin->handleCommand( text );
	appendLog( text + " => " + result );
	m_input->clear();
}

void AgentControlView::appendLog( const QString &text )
{
	m_log->append( text );
}

void AgentControlView::setVoiceUiState( bool recording )
{
	m_voiceStartButton->setEnabled( !recording );
	m_voiceStopButton->setEnabled( recording );
	if( recording )
	{
		m_status->setText( tr( "Recording voice command..." ) );
	}
	else
	{
		m_status->setText( tr( "Listening on 127.0.0.1:7777" ) );
	}
}

QString AgentControlView::ffmpegPath() const
{
	const QString value = qEnvironmentVariable( "LMMS_FFMPEG_BIN" );
	return value.isEmpty() ? QStringLiteral( "ffmpeg" ) : value;
}

QString AgentControlView::whisperPath() const
{
	const QString value = qEnvironmentVariable( "LMMS_WHISPER_CLI" );
	if( !value.isEmpty() )
	{
		return value;
	}

	const QString localWrapper = QDir::home().filePath(
		QStringLiteral( ".lmms/whisper/lmms-macos-transcribe" ) );
	const QFileInfo wrapperInfo( localWrapper );
	if( wrapperInfo.exists() && wrapperInfo.isExecutable() )
	{
		return localWrapper;
	}

	return QStringLiteral( "whisper-cli" );
}

QString AgentControlView::whisperModelPath() const
{
	const QString configured = qEnvironmentVariable( "LMMS_WHISPER_MODEL" );
	if( !configured.isEmpty() && QFileInfo::exists( configured ) )
	{
		return configured;
	}

	const QStringList candidates =
	{
		QDir::home().filePath( QStringLiteral( ".lmms/whisper/ggml-base.en.bin" ) ),
		QDir::home().filePath( QStringLiteral( ".lmms/whisper/ggml-tiny.en.bin" ) ),
		QStringLiteral( "/opt/homebrew/share/whisper/ggml-base.en.bin" ),
		QStringLiteral( "/opt/homebrew/share/whisper/ggml-small.en.bin" ),
		QStringLiteral( "/opt/homebrew/share/whisper/ggml-tiny.en.bin" )
	};
	for( const QString& path : candidates )
	{
		if( QFileInfo::exists( path ) )
		{
			return path;
		}
	}
	return {};
}

QString AgentControlView::whisperServiceBaseUrl() const
{
	const QString explicitUrl = qEnvironmentVariable( "LMMS_WHISPER_SERVICE_URL" ).trimmed();
	if( !explicitUrl.isEmpty() )
	{
		return explicitUrl;
	}

	const QString host = qEnvironmentVariable( "LMMS_WHISPER_SERVICE_HOST" ).trimmed().isEmpty()
		? QStringLiteral( "127.0.0.1" )
		: qEnvironmentVariable( "LMMS_WHISPER_SERVICE_HOST" ).trimmed();
	const int envPort = qEnvironmentVariableIntValue( "LMMS_WHISPER_SERVICE_PORT" );
	const int port = envPort > 0 ? envPort : 50060;
	return QStringLiteral( "http://%1:%2" ).arg( host ).arg( port );
}

QString AgentControlView::whisperServiceModel() const
{
	const QString model = qEnvironmentVariable( "LMMS_WHISPERKIT_MODEL" ).trimmed();
	return model.isEmpty() ? QStringLiteral( "tiny" ) : model;
}

bool AgentControlView::useWhisperService() const
{
	const QString mode = qEnvironmentVariable( "LMMS_WHISPER_MODE" ).trimmed().toLower();
	if( mode == QStringLiteral( "server" ) || mode == QStringLiteral( "service" ) ||
		mode == QStringLiteral( "persistent" ) )
	{
		return true;
	}
	if( mode == QStringLiteral( "cli" ) || mode == QStringLiteral( "command" ) )
	{
		return false;
	}
	if( !qEnvironmentVariable( "LMMS_WHISPER_SERVICE_URL" ).trimmed().isEmpty() )
	{
		return true;
	}

#if defined( Q_OS_MACOS )
	// Auto-enable local server mode on macOS when using WhisperKit wrapper.
	return QFileInfo( whisperPath() ).fileName().toLower() == QStringLiteral( "lmms-macos-transcribe" ) &&
		!QStandardPaths::findExecutable( QStringLiteral( "whisperkit-cli" ) ).isEmpty();
#else
	return false;
#endif
}

bool AgentControlView::checkWhisperServiceHealth( int timeoutMs, QString& error ) const
{
	error.clear();

	QNetworkAccessManager manager;
	QNetworkRequest request{ QUrl( whisperServiceBaseUrl() + QStringLiteral( "/health" ) ) };
	QNetworkReply *reply = manager.get( request );
	QEventLoop loop;
	QObject::connect( reply, &QNetworkReply::finished, &loop, &QEventLoop::quit );
	QTimer::singleShot( qMax( 200, timeoutMs ), &loop, &QEventLoop::quit );
	loop.exec();

	if( reply->isRunning() )
	{
		reply->abort();
		error = tr( "whisper service health check timed out" );
		reply->deleteLater();
		return false;
	}
	if( reply->error() != QNetworkReply::NoError )
	{
		error = reply->errorString();
		reply->deleteLater();
		return false;
	}
	reply->deleteLater();
	return true;
}

bool AgentControlView::ensureWhisperServiceReady( QString& error )
{
	error.clear();

	if( !qEnvironmentVariable( "LMMS_WHISPER_SERVICE_URL" ).trimmed().isEmpty() )
	{
		return checkWhisperServiceHealth( 1200, error );
	}

	if( m_whisperServiceProcess != nullptr && m_whisperServiceProcess->state() != QProcess::NotRunning )
	{
		return checkWhisperServiceHealth( 1200, error );
	}

	const QString serviceBin = qEnvironmentVariable( "LMMS_WHISPER_SERVICE_BIN" ).trimmed().isEmpty()
		? QStringLiteral( "whisperkit-cli" )
		: qEnvironmentVariable( "LMMS_WHISPER_SERVICE_BIN" ).trimmed();

	QString resolvedBin = serviceBin;
	if( QFileInfo( serviceBin ).isRelative() )
	{
		const QString found = QStandardPaths::findExecutable( serviceBin );
		if( !found.isEmpty() )
		{
			resolvedBin = found;
		}
	}
	if( !QFileInfo::exists( resolvedBin ) )
	{
		error = tr( "whisperkit-cli not found for persistent mode" );
		return false;
	}

	const QUrl baseUrl( whisperServiceBaseUrl() );
	const QString host = baseUrl.host().isEmpty() ? QStringLiteral( "127.0.0.1" ) : baseUrl.host();
	const int port = baseUrl.port() > 0 ? baseUrl.port() : 50060;

	m_whisperServiceProcess = new QProcess( this );
	m_whisperServiceProcess->setProcessChannelMode( QProcess::MergedChannels );
	m_whisperServiceProcess->start(
		resolvedBin,
		QStringList{
			QStringLiteral( "serve" ),
			QStringLiteral( "--host" ), host,
			QStringLiteral( "--port" ), QString::number( port ),
			QStringLiteral( "--model" ), whisperServiceModel(),
			QStringLiteral( "--without-timestamps" ),
			QStringLiteral( "--chunking-strategy" ), QStringLiteral( "vad" ),
			QStringLiteral( "--concurrent-worker-count" ), QStringLiteral( "2" )
		} );
	if( !m_whisperServiceProcess->waitForStarted( 2500 ) )
	{
		error = tr( "failed to start whisper service" );
		m_whisperServiceProcess->deleteLater();
		m_whisperServiceProcess = nullptr;
		return false;
	}

	for( int attempt = 0; attempt < 25; ++attempt )
	{
		if( checkWhisperServiceHealth( 300, error ) )
		{
			return true;
		}
		QThread::msleep( 80 );
	}

	const QString serviceOutput = QString::fromUtf8( m_whisperServiceProcess->readAll() ).trimmed();
	shutdownWhisperService();
	error = tr( "whisper service failed to become healthy%1" )
		.arg( serviceOutput.isEmpty() ? QString() : QStringLiteral( ": " ) + serviceOutput );
	return false;
}

bool AgentControlView::transcribeViaWhisperService(
	const QString &audioPath,
	QString &transcript,
	QString& error ) const
{
	transcript.clear();
	error.clear();

	QFileInfo fileInfo( audioPath );
	if( !fileInfo.exists() || fileInfo.size() <= 0 )
	{
		error = tr( "audio file missing for service transcription" );
		return false;
	}

	auto *file = new QFile( audioPath );
	if( !file->open( QIODevice::ReadOnly ) )
	{
		delete file;
		error = tr( "could not open audio for service transcription" );
		return false;
	}

	auto *multi = new QHttpMultiPart( QHttpMultiPart::FormDataType );
	QHttpPart filePart;
	filePart.setHeader( QNetworkRequest::ContentDispositionHeader,
		QStringLiteral( "form-data; name=\"file\"; filename=\"%1\"" ).arg( fileInfo.fileName() ) );
	filePart.setHeader( QNetworkRequest::ContentTypeHeader, QStringLiteral( "audio/wav" ) );
	filePart.setBodyDevice( file );
	file->setParent( multi );
	multi->append( filePart );

	QHttpPart modelPart;
	modelPart.setHeader( QNetworkRequest::ContentDispositionHeader,
		QStringLiteral( "form-data; name=\"model\"" ) );
	modelPart.setBody( whisperServiceModel().toUtf8() );
	multi->append( modelPart );

	QNetworkAccessManager manager;
	QNetworkRequest request{ QUrl( whisperServiceBaseUrl() + QStringLiteral( "/v1/audio/transcriptions" ) ) };
	QNetworkReply *reply = manager.post( request, multi );
	multi->setParent( reply );

	QEventLoop loop;
	QObject::connect( reply, &QNetworkReply::finished, &loop, &QEventLoop::quit );
	QTimer::singleShot( 30000, &loop, &QEventLoop::quit );
	loop.exec();

	if( reply->isRunning() )
	{
		reply->abort();
		error = tr( "whisper service transcription timed out" );
		reply->deleteLater();
		return false;
	}
	if( reply->error() != QNetworkReply::NoError )
	{
		error = reply->errorString();
		reply->deleteLater();
		return false;
	}

	const QByteArray body = reply->readAll();
	reply->deleteLater();
	const QJsonDocument doc = QJsonDocument::fromJson( body );
	if( !doc.isObject() )
	{
		error = tr( "whisper service returned invalid JSON" );
		return false;
	}

	transcript = doc.object().value( "text" ).toString().trimmed();
	if( transcript.isEmpty() )
	{
		error = tr( "whisper service returned empty transcript" );
		return false;
	}
	return true;
}

void AgentControlView::shutdownWhisperService()
{
	if( m_whisperServiceProcess == nullptr )
	{
		return;
	}
	if( m_whisperServiceProcess->state() != QProcess::NotRunning )
	{
		m_whisperServiceProcess->terminate();
		if( !m_whisperServiceProcess->waitForFinished( 600 ) )
		{
			m_whisperServiceProcess->kill();
			m_whisperServiceProcess->waitForFinished( 600 );
		}
	}
	m_whisperServiceProcess->deleteLater();
	m_whisperServiceProcess = nullptr;
}

QString AgentControlView::microphoneDevice() const
{
#if defined( Q_OS_MACOS )
	// On many macOS setups device 0 is an aggregate/virtual mic; built-in mic is often 1.
	const QString fallback = QStringLiteral( "1" );
#else
	const QString fallback = QStringLiteral( "default" );
#endif
	const QString value = qEnvironmentVariable( "LMMS_MIC_DEVICE" );
	return value.isEmpty() ? fallback : value;
}

QStringList AgentControlView::ffmpegCaptureArgs( const QString &outputPath ) const
{
	QStringList args;
	args << QStringLiteral( "-hide_banner" )
		<< QStringLiteral( "-loglevel" ) << QStringLiteral( "error" );
#if defined( Q_OS_MACOS )
	args << QStringLiteral( "-f" ) << QStringLiteral( "avfoundation" )
		<< QStringLiteral( "-i" ) << QStringLiteral( ":" ) + microphoneDevice();
#elif defined( Q_OS_LINUX )
	args << QStringLiteral( "-f" ) << QStringLiteral( "pulse" )
		<< QStringLiteral( "-i" ) << microphoneDevice();
#else
	args << QStringLiteral( "-f" ) << QStringLiteral( "avfoundation" )
		<< QStringLiteral( "-i" ) << QStringLiteral( ":" ) + microphoneDevice();
#endif
	args << QStringLiteral( "-ac" ) << QStringLiteral( "1" )
		<< QStringLiteral( "-ar" ) << QStringLiteral( "16000" )
		<< QStringLiteral( "-y" ) << outputPath;
	return args;
}

void AgentControlView::startVoiceCapture()
{
	if( m_voiceRecordProcess != nullptr )
	{
		appendLog( tr( "Voice capture is already running" ) );
		return;
	}

	const QString tempDir = QStandardPaths::writableLocation( QStandardPaths::TempLocation );
	if( tempDir.isEmpty() )
	{
		appendLog( tr( "Voice capture failed: no temp directory" ) );
		return;
	}
	m_voiceAudioPath = QDir( tempDir ).filePath(
		QStringLiteral( "lmms_agent_voice_%1.wav" )
			.arg( QDateTime::currentMSecsSinceEpoch() ) );

	m_voiceRecordProcess = new QProcess( this );
	connect( m_voiceRecordProcess,
		QOverload<QProcess::ProcessError>::of( &QProcess::errorOccurred ),
		this,
		&AgentControlView::voiceProcessError );

	const QString ffmpeg = ffmpegPath();
	m_voiceRecordProcess->start( ffmpeg, ffmpegCaptureArgs( m_voiceAudioPath ) );
	if( !m_voiceRecordProcess->waitForStarted( 1500 ) )
	{
		appendLog( tr( "Voice capture failed: could not start %1" ).arg( ffmpeg ) );
		m_voiceRecordProcess->deleteLater();
		m_voiceRecordProcess = nullptr;
		m_voiceAudioPath.clear();
		setVoiceUiState( false );
		return;
	}

	appendLog( tr( "Voice recording started. Click Stop + Run when done." ) );
	setVoiceUiState( true );
}

void AgentControlView::stopVoiceCapture()
{
	if( m_voiceRecordProcess == nullptr )
	{
		appendLog( tr( "Voice capture is not running" ) );
		setVoiceUiState( false );
		return;
	}

	if( m_voiceRecordProcess->state() != QProcess::NotRunning )
	{
		m_voiceRecordProcess->write( "q\n" );
		m_voiceRecordProcess->closeWriteChannel();
		if( !m_voiceRecordProcess->waitForFinished( 2500 ) )
		{
			m_voiceRecordProcess->terminate();
			if( !m_voiceRecordProcess->waitForFinished( 1500 ) )
			{
				m_voiceRecordProcess->kill();
				m_voiceRecordProcess->waitForFinished( 1000 );
			}
		}
	}

	const QString ffmpegErrors = QString::fromUtf8( m_voiceRecordProcess->readAllStandardError() ).trimmed();
	if( !ffmpegErrors.isEmpty() )
	{
		appendLog( tr( "ffmpeg: %1" ).arg( ffmpegErrors ) );
	}

	m_voiceRecordProcess->deleteLater();
	m_voiceRecordProcess = nullptr;
	setVoiceUiState( false );

	const QFileInfo audioInfo( m_voiceAudioPath );
	if( !audioInfo.exists() || audioInfo.size() <= 0 )
	{
		appendLog( tr( "Voice capture failed: no audio recorded" ) );
		m_voiceAudioPath.clear();
		return;
	}

	appendLog( tr( "Transcribing voice command..." ) );
	const QString audioPath = m_voiceAudioPath;
	m_voiceAudioPath.clear();
	if( !runWhisperAndDispatch( audioPath ) )
	{
		appendLog( tr( "Voice command failed" ) );
	}
	QFile::remove( audioPath );
	QFile::remove( audioPath + QStringLiteral( ".txt" ) );
}

bool AgentControlView::runWhisperAndDispatch( const QString &audioPath )
{
	QString transcript;
	if( useWhisperService() )
	{
		QString serviceError;
		if( ensureWhisperServiceReady( serviceError ) &&
			transcribeViaWhisperService( audioPath, transcript, serviceError ) )
		{
			appendLog( tr( "Whisper service transcription completed." ) );
		}
		else if( !serviceError.isEmpty() )
		{
			appendLog( tr( "Whisper service unavailable: %1. Falling back to CLI." ).arg( serviceError ) );
		}
	}

	if( transcript.isEmpty() )
	{
		const QString whisperCmd = whisperPath();
		QStringList whisperArgs{
			QStringLiteral( "-f" ), audioPath,
			QStringLiteral( "-otxt" ),
			QStringLiteral( "-nt" ) };

		const QFileInfo whisperInfo( whisperCmd );
		const QString binaryName = whisperInfo.fileName().toLower();
		if( binaryName == QStringLiteral( "whisper-cli" ) )
		{
			const QString modelPath = whisperModelPath();
			if( modelPath.isEmpty() )
			{
				appendLog( tr( "No Whisper model found. Set LMMS_WHISPER_MODEL or LMMS_WHISPER_CLI." ) );
				return false;
			}
			whisperArgs << QStringLiteral( "-m" ) << modelPath;
		}

		QProcess whisperProcess;
		whisperProcess.start( whisperCmd, whisperArgs );
		if( !whisperProcess.waitForStarted( 2000 ) )
		{
			appendLog( tr( "Could not start %1. Set LMMS_WHISPER_CLI." ).arg( whisperCmd ) );
			return false;
		}
		if( !whisperProcess.waitForFinished( 60000 ) )
		{
			whisperProcess.kill();
			whisperProcess.waitForFinished( 1000 );
			appendLog( tr( "Whisper timed out" ) );
			return false;
		}

		transcript = QString::fromUtf8( whisperProcess.readAllStandardOutput() ).trimmed();
		if( transcript.isEmpty() )
		{
			QFile transcriptFile( audioPath + QStringLiteral( ".txt" ) );
			if( transcriptFile.open( QIODevice::ReadOnly | QIODevice::Text ) )
			{
				transcript = QString::fromUtf8( transcriptFile.readAll() ).trimmed();
			}
		}
		if( transcript.isEmpty() )
		{
			const QString whisperErrors = QString::fromUtf8( whisperProcess.readAllStandardError() ).trimmed();
			appendLog( tr( "Whisper returned no transcript%1" )
				.arg( whisperErrors.isEmpty() ? QString() : QStringLiteral( ": " ) + whisperErrors ) );
			if( whisperErrors.contains( QStringLiteral( "whisperkit transcription failed" ), Qt::CaseInsensitive ) )
			{
				appendLog( tr( "No speech detected. Try speaking louder/longer and set LMMS_MIC_DEVICE (macOS often uses 1)." ) );
			}
			return false;
		}
	}

	appendLog( tr( "Heard: %1" ).arg( transcript ) );
	m_input->setText( transcript );
	runCommand();
	return true;
}

void AgentControlView::voiceProcessError( QProcess::ProcessError error )
{
	if( m_voiceRecordProcess == nullptr )
	{
		return;
	}

	if( error == QProcess::FailedToStart )
	{
		appendLog( tr( "Voice capture failed to start. Ensure %1 is installed." ).arg( ffmpegPath() ) );
	}
}

} // namespace lmms::gui
