#include "spotify.hpp"

using namespace spt;

Spotify::Spotify()
{
	lastAuth = 0;
	networkManager = new QNetworkAccessManager();
	refresh();
}

Spotify::~Spotify()
{
	delete networkManager;
}

QNetworkRequest Spotify::request(const QString &url)
{
	// See when last refresh was
	auto lastRefresh = QDateTime::currentSecsSinceEpoch() - lastAuth;
	if (lastRefresh >= 3600)
	{
		qDebug() << "access token probably expired, refreshing";
		refresh();
	}
	// Prepare request
	QNetworkRequest request((QUrl("https://api.spotify.com/v1/" + url)));
	// Set header
	request.setRawHeader("Authorization", ("Bearer " + Settings().accessToken()).toUtf8());
	// Return prepared header
	return request;
}

QJsonDocument Spotify::get(QString url)
{
	// Send request
	auto reply = networkManager->get(request(url));
	// Wait for request to finish
	while (!reply->isFinished())
		QCoreApplication::processEvents();
	// Parse reply as json
	auto json = QJsonDocument::fromJson(reply->readAll());
	reply->deleteLater();
	// Return parsed json
	return json;
}

void Spotify::getLater(const QString &url)
{
	// Prepare fetch of request
	auto context = new QObject();
	QNetworkAccessManager::connect(networkManager, &QNetworkAccessManager::finished, context, [this, context, url](QNetworkReply *reply) {
		auto replyUrl = reply->url().toString();
		if (replyUrl.right(replyUrl.length() - 27) != url)
			return;
		delete context;
		// Parse reply as json
		auto json = QJsonDocument::fromJson(reply->readAll());
		reply->deleteLater();
		emit got(json);
	});
	networkManager->get(request(url));
}

QString Spotify::put(QString url, QVariantMap *body)
{
	// Set in header we're sending json data
	auto req = request(url);
	req.setHeader(QNetworkRequest::ContentTypeHeader, QString("application/json"));
	// Send the request, we don't expect any response
	auto putData = body == nullptr ? nullptr : QJsonDocument::fromVariant(*body).toJson();
	return errorMessage(networkManager->put(req, putData));
}

QString Spotify::post(QString url)
{
	auto req = request(url);
	req.setHeader(QNetworkRequest::ContentTypeHeader, "application/x-www-form-urlencoded");
	return errorMessage(networkManager->post(req, QByteArray()));
}

QString Spotify::del(QString url, QVariantMap *body)
{
	auto req = request(url);
	req.setHeader(QNetworkRequest::ContentTypeHeader, QString("application/json"));
	auto delData = body == nullptr ? nullptr : QJsonDocument::fromVariant(*body).toJson();
	return errorMessage(networkManager->sendCustomRequest(req, "DELETE", delData));
}

QString Spotify::errorMessage(QNetworkReply *reply)
{
	while (!reply->isFinished())
		QCoreApplication::processEvents();
	auto replyBody = reply->readAll();
	reply->deleteLater();
	if (replyBody.isEmpty())
		return QString();
	auto json = QJsonDocument::fromJson(replyBody);
	return json["error"].toObject()["message"].toString();
}

bool Spotify::refresh()
{
	// Make sure we have a refresh token
	Settings settings;
	auto refreshToken = settings.refreshToken();
	if (refreshToken.isEmpty())
	{
		qWarning() << "warning: attempt to refresh without refresh token";
		return false;
	}
	// Create form
	auto postData = QString("grant_type=refresh_token&refresh_token=%1")
		.arg(refreshToken)
		.toUtf8();
	// Create request
	QNetworkRequest request(QUrl("https://accounts.spotify.com/api/token"));
	request.setHeader(QNetworkRequest::ContentTypeHeader, "application/x-www-form-urlencoded");
	request.setRawHeader(
		"Authorization",
		"Basic " + QString("%1:%2")
		.arg(settings.clientId()).arg(settings.clientSecret()).toUtf8().toBase64());
	// Send request
	auto reply = networkManager->post(request, postData);
	while (!reply->isFinished())
		QCoreApplication::processEvents();
	// Parse json
	auto json = QJsonDocument::fromJson(reply->readAll()).object();
	reply->deleteLater();
	// Check if error
	if (json.contains("error_description"))
	{
		qWarning() << "warning: failed to refresh token:" << json["error_description"];
		return false;
	}
	// Save as access token
	lastAuth = QDateTime::currentSecsSinceEpoch();
	auto accessToken = json["access_token"].toString();
	settings.setAccessToken(accessToken);
	return true;
}

void Spotify::playlists(QVector<Playlist> **playlists)
{
	// Request playlists
	auto json = get("me/playlists?limit=50");
	// Parse as playlists
	auto items = json["items"].toArray();
	// Create list of playlists
	auto size = json["total"].toInt();
	(*playlists)->clear();
	(*playlists)->reserve(size);
	// Loop through all items
	for (int i = 0; i < items.size(); i++)
		(*playlists)->insert(i, Playlist(items.at(i).toObject()));
}

QVector<Device> Spotify::devices()
{
	auto json = get("me/player/devices");
	auto items = json["devices"].toArray();
	QVector<Device> devices(items.size());
	for (int i = 0; i < items.size(); i++)
	{
		auto data = items.at(i).toObject();
		Device device;
		device.id				= data["id"].toString();
		device.name				= data["name"].toString();
		device.type				= data["type"].toString();
		device.isActive			= data["is_active"].toBool();
		device.isPrivateSession	= data["is_private_session"].toBool();
		device.isRestricted		= data["is_restricted"].toBool();
		device.volumePercent	= data["volume_percent"].toInt();
		devices[i] = device;
	}
	return devices;
}

QString Spotify::setDevice(const Device &device)
{
	QVariantMap body;
	body["device_ids"] = QStringList({
		device.id
	});
	currentDevice = device.id;
	return put("me/player", &body);
}

QString Spotify::playTracks(const QString &track, const QString &context)
{
	QVariantMap body;
	body["context_uri"] = context;
	body["offset"] = QJsonObject({
		QPair<QString, QJsonValue>("uri", track)
	});
	return put(currentDevice == nullptr
		? QString("me/player/play")
		: QString("me/player/play?device_id=%1").arg(currentDevice), &body);
}

QString Spotify::playTracks(const QString &track, const QStringList &all)
{
	QVariantMap body;
	body["uris"] = all;
	body["offset"] = QJsonObject({
		QPair<QString, QJsonValue>("uri", track)
	});
	return put(currentDevice == nullptr || currentDevice.isEmpty()
		? QString("me/player/play")
		: QString("me/player/play?device_id=%1").arg(currentDevice), &body);
}

QString Spotify::playTracks(const QString &context)
{
	QVariantMap body;
	body["context_uri"] = context;
	return put(currentDevice == nullptr
		? QString("me/player/play")
		: QString("me/player/play?device_id=%1").arg(currentDevice), &body);
}

QString Spotify::setShuffle(bool enabled)
{
	return put(QString("me/player/shuffle?state=%1").arg(enabled ? "true" : "false"));
}

QString Spotify::pause()
{
	return put("me/player/pause");
}

QString Spotify::resume()
{
	return put("me/player/play");
}

QString Spotify::seek(int position)
{
	return put(QString("me/player/seek?position_ms=%1").arg(position));
}

QString Spotify::next()
{
	return post("me/player/next");
}

QString Spotify::previous()
{
	return post("me/player/previous");
}

QString Spotify::setVolume(int volume)
{
	return put(QString("me/player/volume?volume_percent=%1").arg(volume));
}

QString Spotify::setRepeat(const QString &state)
{
	return put(QString("me/player/repeat?state=%1").arg(state));
}

AudioFeatures Spotify::trackAudioFeatures(QString trackId)
{
	auto json = get(QString("audio-features/%1")
		.arg(trackId.startsWith("spotify:track:")
			? trackId.remove(0, QString("spotify:track:").length())
			: trackId));
	return AudioFeatures(json.object());
}

QVector<Track> Spotify::albumTracks(const QString &albumId, const QString &albumName, int offset)
{
	auto json = get(QString("albums/%1/tracks?limit=50&offset=%2").arg(albumId).arg(offset)).object();
	auto trackItems = json["items"].toArray();
	QVector<Track> tracks;
	tracks.reserve(50);
	// Add all in current page
	for (auto item : trackItems)
	{
		auto t = Track(item.toObject());
		// Album name is not included, so we have to set it manually
		t.album = albumName;
		tracks.append(t);
	}
	// Add all in next page
	if (json.contains("next") && !json["next"].isNull())
		tracks.append(albumTracks(albumId, albumName, json["offset"].toInt() + json["limit"].toInt()));
	return tracks;
}

QVector<Track> *Spotify::albumTracks(const QString &albumID)
{
	auto json = get(QString("albums/%1").arg(albumID));
	auto albumName = json["name"].toString();
	auto tracks = new QVector<Track>();
	tracks->reserve(json["total_tracks"].toInt());
	// Loop through all items
	for (auto track : json["tracks"].toObject()["items"].toArray())
	{
		auto t = Track(track.toObject());
		// Album name is not included, so we have to set it manually
		t.album = albumName;
		tracks->append(t);
	}
	// Check if we have any more to add
	auto tracksLimit = json["tracks"].toObject()["limit"].toInt();
	if (json["tracks"].toObject()["total"].toInt() > tracksLimit)
		tracks->append(albumTracks(albumID, albumName, tracksLimit));
	// Return final vector
	return tracks;
}

Artist Spotify::artist(const QString &artistId)
{
	return Artist(get(QString("artists/%1").arg(artistId)).object());
}

Playlist Spotify::playlist(const QString &playlistId)
{
	return Playlist(get(QString("playlists/%1").arg(playlistId)).object());
}

QString Spotify::addToPlaylist(const QString &playlistId, const QString &trackId)
{
	return post(QString("playlists/%1/tracks?uris=%2")
		.arg(playlistId).arg(trackId));
}

QString Spotify::removeFromPlaylist(const QString &playlistId, const QString &trackId, int pos)
{
	QVariantMap body;
	body["tracks"] = QJsonArray({
		QJsonObject({
			QPair<QString, QString>("uri", trackId),
			QPair<QString, QJsonArray>("positions", QJsonArray({
				pos
			}))
		})
	});
	return del(QString("playlists/%1/tracks").arg(playlistId), &body);
}

SearchResults Spotify::search(const QString &query)
{
	auto json = get(
		QString("search?q=%1&type=album,artist,playlist,track&limit=50&market=from_token").arg(query));
	SearchResults results;
	// Albums
	results.albums.reserve(json["albums"].toObject()["total"].toInt());
	for (auto album : json["albums"].toObject()["items"].toArray())
		results.albums.append(Album(album.toObject()));
	// Artists
	results.artists.reserve(json["artists"].toObject()["total"].toInt());
	for (auto artist : json["artists"].toObject()["items"].toArray())
		results.artists.append(Artist(artist.toObject()));
	// Playlists
	results.playlists.reserve(json["playlists"].toObject()["total"].toInt());
	for (auto playlist : json["playlists"].toObject()["items"].toArray())
		results.playlists.append(playlist.toObject());
	// Tracks
	results.tracks.reserve(json["tracks"].toObject()["total"].toInt());
	for (auto track : json["tracks"].toObject()["items"].toArray())
		results.tracks.append(Track(track.toObject()));

	return results;
}

template<class T>
QVector<T> Spotify::loadItems(const QString &url)
{
	auto items = get(url).object()["items"].toArray();
	QVector<T> result;
	result.reserve(items.count());
	for (auto item : items)
		result.append(T(item.toObject()));
	return result;
}

QVector<Artist> Spotify::topArtists()
{
	return loadItems<Artist>("me/top/artists?limit=10");
}

QVector<Track> Spotify::topTracks()
{
	return loadItems<Track>("me/top/tracks?limit=50");
}

QVector<Track> Spotify::recentlyPlayed()
{
	return loadItems<Track>("me/player/recently-played?limit=50");
}

QVector<Album> Spotify::savedAlbums()
{
	auto json = get("me/albums");
	auto albumItems = json.object()["items"].toArray();
	QVector<Album> albums;
	albums.reserve(10);
	for (auto item : albumItems)
		albums.append(Album(item.toObject()["album"].toObject()));
	return albums;
}

QVector<Track> Spotify::savedTracks(int offset)
{
	auto json = get(QString("me/tracks?limit=50&offset=%1").arg(offset)).object();
	auto trackItems = json["items"].toArray();
	QVector<Track> tracks;
	tracks.reserve(50);
	// Add all in current page
	for (auto item : trackItems)
		tracks.append(Track(item.toObject()));
	// Add all in next page
	if (json.contains("next") && !json["next"].isNull())
		tracks.append(savedTracks(json["offset"].toInt() + json["limit"].toInt()));
	return tracks;
}

QString Spotify::addSavedTrack(const QString &trackId)
{
	QVariantMap body;
	body["ids"] = QStringList({
		trackId.right(trackId.length() - QString("spotify:track:").length())
	});
	return put("me/tracks", &body);
}

QString Spotify::removeSavedTrack(const QString &trackId)
{
	QVariantMap body;
	body["ids"] = QStringList({
		trackId.right(trackId.length() - QString("spotify:track:").length())
	});
	return del("me/tracks", &body);
}

QVector<Album> Spotify::newReleases()
{
	auto json = get("browse/new-releases");
	auto albumItems = json.object()["albums"].toObject()["items"].toArray();
	QVector<Album> albums;
	albums.reserve(albumItems.size());
	for (auto item : albumItems)
		albums.append(Album(item.toObject()));
	return albums;
}

void Spotify::requestCurrentPlayback()
{
	auto context = new QObject();
	Spotify::connect(this, &Spotify::got, context, [this, context](const QJsonDocument &json) {
		delete context;
		emit gotPlayback(Playback(json.object()));
	});
	getLater("me/player");
}
