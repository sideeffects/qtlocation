/****************************************************************************
**
** Copyright (C) 2021 The Qt Company Ltd.
** Contact: https://www.qt.io/licensing/
**
** This file is part of the examples of the Qt Toolkit.
**
** $QT_BEGIN_LICENSE:BSD$
** Commercial License Usage
** Licensees holding valid commercial Qt licenses may use this file in
** accordance with the commercial license agreement provided with the
** Software or, alternatively, in accordance with the terms contained in
** a written agreement between you and The Qt Company. For licensing terms
** and conditions see https://www.qt.io/terms-conditions. For further
** information use the contact form at https://www.qt.io/contact-us.
**
** BSD License Usage
** Alternatively, you may use this file under the terms of the BSD license
** as follows:
**
** "Redistribution and use in source and binary forms, with or without
** modification, are permitted provided that the following conditions are
** met:
**   * Redistributions of source code must retain the above copyright
**     notice, this list of conditions and the following disclaimer.
**   * Redistributions in binary form must reproduce the above copyright
**     notice, this list of conditions and the following disclaimer in
**     the documentation and/or other materials provided with the
**     distribution.
**   * Neither the name of The Qt Company Ltd nor the names of its
**     contributors may be used to endorse or promote products derived
**     from this software without specific prior written permission.
**
**
** THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
** "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
** LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
** A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
** OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
** SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
** LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
** DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
** THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
** (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
** OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE."
**
** $QT_END_LICENSE$
**
****************************************************************************/

#include "openweathermapbackend.h"

#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QUrlQuery>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonValue>
#include <QJsonArray>
#include <QGeoCoordinate>
#include <QLoggingCategory>

Q_DECLARE_LOGGING_CATEGORY(requestsLog)

static constexpr auto kZeroKelvin = 273.15;

static QString niceTemperatureString(double t)
{
    return QString::number(qRound(t - kZeroKelvin)) + QChar(0xB0);
}

static void parseWeatherDescription(const QJsonObject &object, WeatherInfo &info)
{
    const QJsonArray weatherArray = object.value(u"weather").toArray();
    if (!weatherArray.isEmpty()) {
        const QJsonObject obj = weatherArray.first().toObject();
        info.m_weatherDescription = obj.value(u"description").toString();
        // TODO - convert to some common string
        info.m_weatherIconId = obj.value(u"icon").toString();
    } else {
        qCDebug(requestsLog, "An empty weather array is returned.");
    }
}

OpenWeatherMapBackend::OpenWeatherMapBackend(QObject *parent)
    : ProviderBackend(parent),
      m_networkManager(new QNetworkAccessManager(this)),
      m_appId(QStringLiteral("36496bad1955bf3365448965a42b9eac"))
{
}

void OpenWeatherMapBackend::requestWeatherInfo(const QString &city)
{
    QUrlQuery query;
    query.addQueryItem(QStringLiteral("q"), city);

    requestCurrentWeather(query);
}

void OpenWeatherMapBackend::requestWeatherInfo(const QGeoCoordinate &coordinate)
{
    QUrlQuery query;
    query.addQueryItem(QStringLiteral("lat"), QString::number(coordinate.latitude()));
    query.addQueryItem(QStringLiteral("lon"), QString::number(coordinate.longitude()));

    requestCurrentWeather(query);
}

void OpenWeatherMapBackend::handleCurrentWeatherReply(QNetworkReply *reply)
{
    if (!reply) {
        emit errorOccurred();
        return;
    }
    bool parsed = false;
    if (!reply->error()) {
        // extract info about current weather
        const QJsonDocument document = QJsonDocument::fromJson(reply->readAll());
        const QJsonObject documentObject = document.object();

        const QString city = documentObject.value(u"name").toString();
        qCDebug(requestsLog) << "Got current weather for" << city;

        WeatherInfo currentWeather;

        parseWeatherDescription(documentObject, currentWeather);

        const QJsonObject mainObject = documentObject.value(u"main").toObject();
        const QJsonValue tempValue = mainObject.value(u"temp");
        if (tempValue.isDouble())
            currentWeather.m_temperature = niceTemperatureString(tempValue.toDouble());
        else
            qCDebug(requestsLog, "Failed to parse current temperature.");

        parsed = !city.isEmpty() && !currentWeather.m_temperature.isEmpty();

        if (parsed) {
            // request forecast
            requestWeatherForecast(city, currentWeather);
        }
    }
    if (!parsed) {
        emit errorOccurred();
        if (reply->error())
            qCDebug(requestsLog) << reply->errorString();
        else
            qCDebug(requestsLog, "Failed to parse current weather JSON.");
    }

    reply->deleteLater();
}

void OpenWeatherMapBackend::handleWeatherForecastReply(QNetworkReply *reply, const QString &city,
                                                       const WeatherInfo &currentWeather)
{
    if (!reply) {
        emit errorOccurred();
        return;
    }
    if (!reply->error()) {
        QJsonDocument document = QJsonDocument::fromJson(reply->readAll());
        const QJsonObject documentObject = document.object();

        QList<WeatherInfo> weatherDetails;
        // current weather will be the first in the list
        weatherDetails << currentWeather;

        QJsonArray daysList = documentObject.value(u"list").toArray();
        // include current day as well
        for (qsizetype i = 0; i < daysList.size(); ++i) {
            QJsonObject dayObject = daysList.at(i).toObject();
            WeatherInfo info;

            const QDateTime dt = QDateTime::fromSecsSinceEpoch(dayObject.value(u"dt").toInteger());
            info.m_dayOfWeek = dt.toString(u"ddd");

            const QJsonObject tempObject = dayObject.value(u"temp").toObject();

            const QJsonValue minTemp = tempObject.value(u"min");
            const QJsonValue maxTemp = tempObject.value(u"max");
            if (minTemp.isDouble() && maxTemp.isDouble()) {
                info.m_temperature = niceTemperatureString(minTemp.toDouble()) + QChar('/')
                        + niceTemperatureString(maxTemp.toDouble());
            } else {
                qCDebug(requestsLog, "Failed to parse min or max temperature.");
            }

            parseWeatherDescription(dayObject, info);

            if (!info.m_temperature.isEmpty() && !info.m_weatherIconId.isEmpty())
                weatherDetails.push_back(info);
        }

        emit weatherInformation(city, weatherDetails);
    } else {
        emit errorOccurred();
        qCDebug(requestsLog) << reply->errorString();
    }

    reply->deleteLater();
}

void OpenWeatherMapBackend::requestCurrentWeather(QUrlQuery &query)
{
    QUrl url("http://api.openweathermap.org/data/2.5/weather");
    query.addQueryItem(QStringLiteral("mode"), QStringLiteral("json"));
    query.addQueryItem(QStringLiteral("APPID"), m_appId);
    url.setQuery(query);

    QNetworkReply *reply = m_networkManager->get(QNetworkRequest(url));
    connect(reply, &QNetworkReply::finished, this,
            [this, reply]() { handleCurrentWeatherReply(reply); });
}

void OpenWeatherMapBackend::requestWeatherForecast(const QString &city,
                                                   const WeatherInfo &currentWeather)
{
    QUrl url("http://api.openweathermap.org/data/2.5/forecast/daily");
    QUrlQuery query;
    query.addQueryItem(QStringLiteral("q"), city);
    query.addQueryItem(QStringLiteral("mode"), QStringLiteral("json"));
    query.addQueryItem(QStringLiteral("cnt"), QStringLiteral("4"));
    query.addQueryItem(QStringLiteral("APPID"), m_appId);
    url.setQuery(query);

    QNetworkReply *reply = m_networkManager->get(QNetworkRequest(url));
    connect(reply, &QNetworkReply::finished, this, [this, reply, city, currentWeather]() {
        handleWeatherForecastReply(reply, city, currentWeather);
    });
}