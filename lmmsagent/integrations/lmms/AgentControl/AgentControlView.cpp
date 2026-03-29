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
#include <QRegularExpression>
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
	, m_voiceStopButton( new QPushButton( tr( "Stop Voice" ), this ) )
	, m_voiceRecordProcess( nullptr )
	, m_whisperServiceProcess( nullptr )
	, m_voiceChunkTimer( new QTimer( this ) )
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
	connect( m_voiceChunkTimer, &QTimer::timeout, this, &AgentControlView::processVoiceChunks );
	const int configuredPollMs = qEnvironmentVariableIntValue( "LMMS_VOICE_POLL_MS" );
	m_voiceChunkTimer->setInterval( configuredPollMs > 0 ? qMax( 200, configuredPollMs ) : 350 );
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
	if( m_voiceChunkTimer != nullptr )
	{
		m_voiceChunkTimer->stop();
	}
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
	if( !m_voiceChunkDir.isEmpty() )
	{
		QDir chunkDir( m_voiceChunkDir );
		chunkDir.removeRecursively();
		m_voiceChunkDir.clear();
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
		m_status->setText( tr( "Voice listener running..." ) );
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
	m_whisperServiceReady = false;
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

QString AgentControlView::voiceChunkDirPath() const
{
	const QString tempDir = QStandardPaths::writableLocation( QStandardPaths::TempLocation );
	if( tempDir.isEmpty() )
	{
		return QString();
	}
	return QDir( tempDir ).filePath(
		QStringLiteral( "lmms_agent_voice_session_%1" )
			.arg( QDateTime::currentMSecsSinceEpoch() ) );
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

QStringList AgentControlView::ffmpegContinuousCaptureArgs( const QString &outputPattern ) const
{
	QStringList args;
	const QString segmentSeconds = qEnvironmentVariable( "LMMS_VOICE_SEGMENT_SEC" ).trimmed();
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
		<< QStringLiteral( "-f" ) << QStringLiteral( "segment" )
		<< QStringLiteral( "-segment_time" ) << ( segmentSeconds.isEmpty() ? QStringLiteral( "1.6" ) : segmentSeconds )
		<< QStringLiteral( "-segment_format" ) << QStringLiteral( "wav" )
		<< QStringLiteral( "-reset_timestamps" ) << QStringLiteral( "1" )
		<< QStringLiteral( "-y" ) << outputPattern;
	return args;
}

void AgentControlView::startVoiceCapture()
{
	if( m_voiceRecordProcess != nullptr )
	{
		appendLog( tr( "Voice capture is already running" ) );
		return;
	}

	m_voiceChunkDir = voiceChunkDirPath();
	if( m_voiceChunkDir.isEmpty() )
	{
		appendLog( tr( "Voice capture failed: no temp directory" ) );
		return;
	}
	if( !QDir().mkpath( m_voiceChunkDir ) )
	{
		appendLog( tr( "Voice capture failed: could not create chunk directory" ) );
		m_voiceChunkDir.clear();
		return;
	}

	m_processedVoiceChunks.clear();
	m_lastDispatchedTranscript.clear();
	m_pendingTranscript.clear();
	m_lastDispatchedAtMs = 0;
	m_voiceAudioPath.clear();
	m_whisperServiceReady = false;

	m_voiceRecordProcess = new QProcess( this );
	connect( m_voiceRecordProcess,
		QOverload<QProcess::ProcessError>::of( &QProcess::errorOccurred ),
		this,
		&AgentControlView::voiceProcessError );

	const QString ffmpeg = ffmpegPath();
	const QString outputPattern = QDir( m_voiceChunkDir ).filePath( QStringLiteral( "chunk_%06d.wav" ) );
	m_voiceRecordProcess->start( ffmpeg, ffmpegContinuousCaptureArgs( outputPattern ) );
	if( !m_voiceRecordProcess->waitForStarted( 1500 ) )
	{
		appendLog( tr( "Voice capture failed: could not start %1" ).arg( ffmpeg ) );
		m_voiceRecordProcess->deleteLater();
		m_voiceRecordProcess = nullptr;
		QDir( m_voiceChunkDir ).removeRecursively();
		m_voiceChunkDir.clear();
		setVoiceUiState( false );
		return;
	}

	if( useWhisperService() )
	{
		QString serviceError;
		if( ensureWhisperServiceReady( serviceError ) )
		{
			m_whisperServiceReady = true;
			appendLog( tr( "Whisper service is warm and ready." ) );
		}
		else
		{
			appendLog( tr( "Whisper service warmup failed: %1. Falling back to CLI transcription." ).arg( serviceError ) );
		}
	}

	m_voiceChunkTimer->start();
	appendLog( tr( "Voice listener started. Relevant commands execute automatically." ) );
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

	if( m_voiceChunkTimer != nullptr )
	{
		m_voiceChunkTimer->stop();
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

	// Process final chunk files after ffmpeg exits.
	processVoiceChunks();

	if( !m_voiceChunkDir.isEmpty() )
	{
		QDir( m_voiceChunkDir ).removeRecursively();
		m_voiceChunkDir.clear();
	}
	m_processedVoiceChunks.clear();
	m_pendingTranscript.clear();
	m_whisperServiceReady = false;
	appendLog( tr( "Voice listener stopped." ) );
}

void AgentControlView::processVoiceChunks()
{
	if( m_voiceChunkDir.isEmpty() )
	{
		return;
	}

	QDir chunkDir( m_voiceChunkDir );
	if( !chunkDir.exists() )
	{
		return;
	}

	const QStringList files = chunkDir.entryList(
		QStringList{ QStringLiteral( "chunk_*.wav" ) },
		QDir::Files,
		QDir::Name );
	const bool isRecording = m_voiceRecordProcess != nullptr &&
		m_voiceRecordProcess->state() != QProcess::NotRunning;

	for( int i = 0; i < files.size(); ++i )
	{
		const QString& fileName = files[i];
		if( m_processedVoiceChunks.contains( fileName ) )
		{
			continue;
		}
		// Skip the newest segment while ffmpeg is still writing.
		if( isRecording && i == files.size() - 1 )
		{
			continue;
		}

		const QString audioPath = chunkDir.filePath( fileName );
		const QFileInfo audioInfo( audioPath );
		if( !audioInfo.exists() || audioInfo.size() < 2048 )
		{
			continue;
		}

		m_processedVoiceChunks.insert( fileName );
		QString transcript;
		if( transcribeAudio( audioPath, transcript, true ) )
		{
			dispatchTranscript( transcript );
		}

		QFile::remove( audioPath );
		QFile::remove( audioPath + QStringLiteral( ".txt" ) );
	}
}

bool AgentControlView::transcribeAudio(
	const QString &audioPath,
	QString &transcript,
	bool quietNoTranscript )
{
	transcript.clear();
	if( useWhisperService() )
	{
		QString serviceError;
		if( !m_whisperServiceReady )
		{
			m_whisperServiceReady = ensureWhisperServiceReady( serviceError );
			if( !m_whisperServiceReady && !quietNoTranscript && !serviceError.isEmpty() )
			{
				appendLog( tr( "Whisper service unavailable: %1. Falling back to CLI." ).arg( serviceError ) );
			}
		}
		if( m_whisperServiceReady && transcribeViaWhisperService( audioPath, transcript, serviceError ) )
		{
			if( !quietNoTranscript )
			{
				appendLog( tr( "Whisper service transcription completed." ) );
			}
		}
		else if( m_whisperServiceReady )
		{
			m_whisperServiceReady = false;
			if( !quietNoTranscript && !serviceError.isEmpty() )
			{
				appendLog( tr( "Whisper service transcription failed: %1. Falling back to CLI." ).arg( serviceError ) );
			}
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
				if( !quietNoTranscript )
				{
					appendLog( tr( "No Whisper model found. Set LMMS_WHISPER_MODEL or LMMS_WHISPER_CLI." ) );
				}
				return false;
			}
			whisperArgs << QStringLiteral( "-m" ) << modelPath;
		}

		QProcess whisperProcess;
		whisperProcess.start( whisperCmd, whisperArgs );
		if( !whisperProcess.waitForStarted( 2000 ) )
		{
			if( !quietNoTranscript )
			{
				appendLog( tr( "Could not start %1. Set LMMS_WHISPER_CLI." ).arg( whisperCmd ) );
			}
			return false;
		}
		if( !whisperProcess.waitForFinished( 60000 ) )
		{
			whisperProcess.kill();
			whisperProcess.waitForFinished( 1000 );
			if( !quietNoTranscript )
			{
				appendLog( tr( "Whisper timed out" ) );
			}
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
			if( !quietNoTranscript )
			{
				appendLog( tr( "Whisper returned no transcript%1" )
					.arg( whisperErrors.isEmpty() ? QString() : QStringLiteral( ": " ) + whisperErrors ) );
				if( whisperErrors.contains( QStringLiteral( "whisperkit transcription failed" ), Qt::CaseInsensitive ) )
				{
					appendLog( tr( "No speech detected. Try speaking louder/longer and set LMMS_MIC_DEVICE (macOS often uses 1)." ) );
				}
			}
			return false;
		}
	}

	return !transcript.trimmed().isEmpty();
}

bool AgentControlView::looksLikeCommandTranscript( const QString& transcript ) const
{
	QString normalized = transcript.toLower().simplified();
	if( normalized.isEmpty() )
	{
		return false;
	}

	normalized.remove( QRegularExpression(
		R"(^(?:(?:hey|hi|yo|okay|ok|please|pls)\s+)*(?:(?:can|could|would)\s+(?:you|u)\s+)?(?:please|pls\s+)?(?:just\s+)?)",
		QRegularExpression::CaseInsensitiveOption ) );
	normalized = normalized.simplified();

	const QStringList rawParts = normalized.split( ' ', Qt::SkipEmptyParts );
	QStringList tokens;
	for( QString token : rawParts )
	{
		token.remove( QRegularExpression( R"(^[^\w]+|[^\w]+$)" ) );
		if( !token.isEmpty() )
		{
			tokens << token;
		}
	}
	if( tokens.isEmpty() )
	{
		return false;
	}

	const QSet<QString> singleCommands = { QStringLiteral( "undo" ), QStringLiteral( "help" ) };
	if( singleCommands.contains( tokens.first() ) )
	{
		return true;
	}

	const QSet<QString> actionKeywords =
	{
		QStringLiteral( "open" ), QStringLiteral( "show" ), QStringLiteral( "new" ),
		QStringLiteral( "create" ), QStringLiteral( "make" ), QStringLiteral( "import" ),
		QStringLiteral( "load" ), QStringLiteral( "set" ), QStringLiteral( "add" ),
		QStringLiteral( "remove" ), QStringLiteral( "mute" ), QStringLiteral( "solo" ),
		QStringLiteral( "undo" ), QStringLiteral( "tempo" ), QStringLiteral( "bpm" ),
		QStringLiteral( "divide" ), QStringLiteral( "split" ), QStringLiteral( "slice" ),
		QStringLiteral( "raise" ), QStringLiteral( "lower" ), QStringLiteral( "increase" ),
		QStringLiteral( "decrease" )
	};
	const QSet<QString> domainKeywords =
	{
		QStringLiteral( "track" ), QStringLiteral( "instrument" ), QStringLiteral( "sample" ),
		QStringLiteral( "automation" ), QStringLiteral( "pattern" ), QStringLiteral( "mixer" ),
		QStringLiteral( "song" ), QStringLiteral( "editor" ), QStringLiteral( "slicer" ),
		QStringLiteral( "segments" ), QStringLiteral( "transient" ), QStringLiteral( "kick" ),
		QStringLiteral( "drums" ), QStringLiteral( "tempo" ), QStringLiteral( "bpm" )
	};

	const bool firstIsAction = actionKeywords.contains( tokens.first() );
	bool hasAction = firstIsAction;
	bool hasDomain = false;
	for( const QString& token : tokens )
	{
		if( actionKeywords.contains( token ) )
		{
			hasAction = true;
		}
		if( domainKeywords.contains( token ) )
		{
			hasDomain = true;
		}
	}

	if( firstIsAction && hasDomain )
	{
		return true;
	}
	if( ( hasAction || hasDomain ) && tokens.size() <= 8 )
	{
		return true;
	}
	if( QRegularExpression( R"(\b\d{2,3}\s*bpm\b)", QRegularExpression::CaseInsensitiveOption )
		.match( normalized ).hasMatch() )
	{
		return true;
	}
	return false;
}

bool AgentControlView::dispatchTranscript( const QString& transcript )
{
	const QString cleanTranscript = transcript.trimmed();
	if( cleanTranscript.isEmpty() )
	{
		return false;
	}

	QStringList candidates;
	candidates << cleanTranscript;
	if( !m_pendingTranscript.isEmpty() )
	{
		candidates << ( m_pendingTranscript + QStringLiteral( " " ) + cleanTranscript ).simplified();
	}

	for( const QString& candidate : candidates )
	{
		if( candidate.isEmpty() )
		{
			continue;
		}
		if( !looksLikeCommandTranscript( candidate ) )
		{
			continue;
		}

		QString commandText = candidate;
		QString fastCommand;
		if( canonicalizeFastCommand( candidate, fastCommand ) )
		{
			commandText = fastCommand;
		}

		const QString normalized = commandText.toLower().simplified();
		const qint64 nowMs = QDateTime::currentMSecsSinceEpoch();
		if( normalized == m_lastDispatchedTranscript && nowMs - m_lastDispatchedAtMs < 2500 )
		{
			return false;
		}

		const QString result = m_plugin->handleCommand( commandText );
		if( result.startsWith( QStringLiteral( "Unknown command:" ), Qt::CaseInsensitive ) ||
			result.startsWith( QStringLiteral( "Unknown window:" ), Qt::CaseInsensitive ) ||
			result.startsWith( QStringLiteral( "Unknown instrument:" ), Qt::CaseInsensitive ) )
		{
			continue;
		}

		appendLog( tr( "Heard: %1" ).arg( cleanTranscript ) );
		if( commandText.compare( cleanTranscript, Qt::CaseInsensitive ) != 0 )
		{
			appendLog( tr( "Fast map: %1" ).arg( commandText ) );
		}
		appendLog( commandText + QStringLiteral( " => " ) + result );
		m_lastDispatchedTranscript = normalized;
		m_lastDispatchedAtMs = nowMs;
		m_pendingTranscript.clear();
		return true;
	}

	const QStringList tokens = cleanTranscript.toLower().simplified().split( ' ', Qt::SkipEmptyParts );
	if( tokens.size() <= 3 )
	{
		m_pendingTranscript = cleanTranscript;
	}
	else
	{
		m_pendingTranscript = tokens.mid( qMax( 0, tokens.size() - 3 ) ).join( ' ' );
	}
	return false;
}

bool AgentControlView::canonicalizeFastCommand( const QString& transcript, QString& command ) const
{
	command.clear();
	const QString normalized = transcript.toLower().simplified();
	if( normalized.isEmpty() )
	{
		return false;
	}

	if( normalized.contains( QStringLiteral( "open" ) ) &&
		( normalized.contains( QStringLiteral( "slicer" ) ) || normalized.contains( QStringLiteral( "splicer" ) ) ) )
	{
		command = QStringLiteral( "open slicer" );
		return true;
	}
	if( normalized == QStringLiteral( "slicer" ) || normalized == QStringLiteral( "splicer" ) )
	{
		command = QStringLiteral( "open slicer" );
		return true;
	}

	if( normalized.contains( QStringLiteral( "import" ) ) &&
		( normalized.contains( QStringLiteral( "slicer" ) ) || normalized.contains( QStringLiteral( "splicer" ) ) ) )
	{
		const QRegularExpression filePattern(
			R"(\b([a-z0-9._-]+\.(?:wav|mp3|flac|aiff|ogg|m4a))\b)",
			QRegularExpression::CaseInsensitiveOption );
		const auto fileMatch = filePattern.match( normalized );
		const QString fileName = fileMatch.hasMatch() ? fileMatch.captured( 1 ) : QStringLiteral( "sample.wav" );
		command = QStringLiteral( "import %1 into slicer" ).arg( fileName );
		return true;
	}

	if( ( normalized.contains( QStringLiteral( "divide" ) ) || normalized.contains( QStringLiteral( "split" ) ) ) &&
		normalized.contains( QStringLiteral( "transient" ) ) )
	{
		command = QStringLiteral( "divide into transient segments" );
		return true;
	}

	if( ( normalized.contains( QStringLiteral( "divide" ) ) || normalized.contains( QStringLiteral( "split" ) ) ) &&
		normalized.contains( QStringLiteral( "equal" ) ) &&
		normalized.contains( QStringLiteral( "segment" ) ) )
	{
		command = QStringLiteral( "divide into equal segments" );
		return true;
	}

	if( ( normalized.contains( QStringLiteral( "create" ) ) || normalized.contains( QStringLiteral( "new" ) ) ||
		  normalized.contains( QStringLiteral( "make" ) ) ) &&
		normalized.contains( QStringLiteral( "instrument" ) ) &&
		normalized.contains( QStringLiteral( "track" ) ) )
	{
		command = QStringLiteral( "create instrument track" );
		return true;
	}

	if( ( normalized.contains( QStringLiteral( "open" ) ) || normalized.contains( QStringLiteral( "show" ) ) ) &&
		normalized.contains( QStringLiteral( "piano" ) ) &&
		( normalized.contains( QStringLiteral( "roll" ) ) || normalized.contains( QStringLiteral( "role" ) ) ) )
	{
		command = QStringLiteral( "open piano roll" );
		return true;
	}

	if( normalized.contains( QStringLiteral( "add" ) ) &&
		( normalized.contains( QStringLiteral( "kick" ) ) || normalized.contains( QStringLiteral( "808" ) ) ||
		  normalized.contains( QStringLiteral( "drum" ) ) ) )
	{
		command = QStringLiteral( "add kick drums" );
		return true;
	}
	if( normalized.contains( QStringLiteral( "kick" ) ) || normalized.contains( QStringLiteral( "808" ) ) )
	{
		command = QStringLiteral( "add kick drums" );
		return true;
	}

	if( ( normalized.contains( QStringLiteral( "open" ) ) || normalized.contains( QStringLiteral( "show" ) ) ) &&
		normalized.contains( QStringLiteral( "song" ) ) &&
		( normalized.contains( QStringLiteral( "editor" ) ) || normalized.contains( QStringLiteral( "edit" ) ) ) )
	{
		command = QStringLiteral( "open song editor" );
		return true;
	}

	const QRegularExpression bpmPattern( R"(\b(\d{2,3})\s*bpm\b)", QRegularExpression::CaseInsensitiveOption );
	const auto bpmMatch = bpmPattern.match( normalized );
	if( bpmMatch.hasMatch() )
	{
		command = QStringLiteral( "set tempo to %1" ).arg( bpmMatch.captured( 1 ) );
		return true;
	}

	return false;
}

bool AgentControlView::runWhisperAndDispatch( const QString &audioPath )
{
	QString transcript;
	if( !transcribeAudio( audioPath, transcript, false ) )
	{
		return false;
	}
	return dispatchTranscript( transcript );
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
