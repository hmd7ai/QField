/***************************************************************************
  positioning.cpp - Positioning

 ---------------------
 begin                : 22.05.2022
 copyright            : (C) 2022 by Mathieu Pellerin
 email                : mathieu at opengis dot ch
 ***************************************************************************
 *                                                                         *
 *   This program is free software; you can redistribute it and/or modify  *
 *   it under the terms of the GNU General Public License as published by  *
 *   the Free Software Foundation; either version 2 of the License, or     *
 *   (at your option) any later version.                                   *
 *                                                                         *
 ***************************************************************************/

#ifdef WITH_BLUETOOTH
#include "bluetoothreceiver.h"
#endif
#ifdef WITH_SERIALPORT
#include "serialportreceiver.h"
#endif
#include "internalgnssreceiver.h"
#include "positioning.h"
#include "positioningutils.h"
#include "tcpreceiver.h"
#include "udpreceiver.h"

#include <qgsunittypes.h>

Positioning::Positioning( QObject *parent )
  : QObject( parent )
{
  // Setup internal gnss receiver by default
  setupDevice();
}

void Positioning::setActive( bool active )
{
  if ( mActive == active )
    return;

  mActive = active;

  if ( mActive )
  {
    if ( !mReceiver )
    {
      setupDevice();
    }
    mReceiver->connectDevice();
  }
  else
  {
    if ( mReceiver )
    {
      mReceiver->disconnectDevice();
    }
  }

  emit activeChanged();
}

void Positioning::setDeviceId( const QString &id )
{
  if ( mDeviceId == id )
    return;

  mDeviceId = id;
  setupDevice();

  emit deviceIdChanged();
}

void Positioning::setValid( bool valid )
{
  if ( mValid == valid )
    return;

  mValid = valid;

  emit validChanged();
}

void Positioning::setAveragedPosition( bool averaged )
{
  if ( mAveragedPosition == averaged )
    return;

  mAveragedPosition = averaged;
  if ( mAveragedPosition )
  {
    mCollectedPositionInformations << mPositionInformation;
  }
  else
  {
    mCollectedPositionInformations.clear();
  }

  emit averagedPositionCountChanged();
  emit averagedPositionChanged();
}

void Positioning::setLogging( bool logging )
{
  if ( mLogging == logging )
    return;

  mLogging = logging;

  if ( mReceiver )
  {
    if ( mLogging )
    {
      mReceiver->startLogging();
    }
    else
    {
      mReceiver->stopLogging();
    }
  }

  emit loggingChanged();
}

void Positioning::setElevationCorrectionMode( ElevationCorrectionMode elevationCorrectionMode )
{
  if ( mElevationCorrectionMode == elevationCorrectionMode )
    return;

  mElevationCorrectionMode = elevationCorrectionMode;

  emit elevationCorrectionModeChanged();
}

void Positioning::setAntennaHeight( double antennaHeight )
{
  if ( mAntennaHeight == antennaHeight )
    return;

  mAntennaHeight = antennaHeight;

  emit antennaHeightChanged();
}

void Positioning::setCoordinateTransformer( QgsQuickCoordinateTransformer *coordinateTransformer )
{
  if ( mCoordinateTransformer == coordinateTransformer )
    return;

  if ( mCoordinateTransformer )
  {
    disconnect( mCoordinateTransformer, &QgsQuickCoordinateTransformer::projectedPositionChanged, this, &Positioning::projectedPositionTransformed );
  }

  mCoordinateTransformer = coordinateTransformer;
  connect( mCoordinateTransformer, &QgsQuickCoordinateTransformer::projectedPositionChanged, this, &Positioning::projectedPositionTransformed );

  emit coordinateTransformerChanged();
}

void Positioning::setupDevice()
{
  if ( mReceiver )
  {
    mReceiver->disconnectDevice();
    mReceiver->stopLogging();
    disconnect( mReceiver, &AbstractGnssReceiver::lastGnssPositionInformationChanged, this, &Positioning::lastGnssPositionInformationChanged );
    mReceiver->deleteLater();
    mReceiver = nullptr;
  }

  if ( mDeviceId.isEmpty() )
  {
    mReceiver = new InternalGnssReceiver( this );
  }
  else
  {
    if ( mDeviceId.startsWith( QStringLiteral( "tcp:" ) ) )
    {
      const qsizetype portSeparator = mDeviceId.lastIndexOf( ':' );
      const QString address = mDeviceId.mid( 4, portSeparator - 4 );
      const int port = mDeviceId.mid( portSeparator + 1 ).toInt();
      mReceiver = new TcpReceiver( address, port, this );
    }
    else if ( mDeviceId.startsWith( QStringLiteral( "udp:" ) ) )
    {
      const qsizetype portSeparator = mDeviceId.lastIndexOf( ':' );
      const QString address = mDeviceId.mid( 4, portSeparator - 4 );
      const int port = mDeviceId.mid( portSeparator + 1 ).toInt();
      mReceiver = new UdpReceiver( address, port, this );
    }
#ifdef WITH_SERIALPORT
    else if ( mDeviceId.startsWith( QStringLiteral( "serial:" ) ) )
    {
      const QString address = mDeviceId.mid( 7 );
      mReceiver = new SerialPortReceiver( address, this );
    }
#endif
    else
    {
#ifdef WITH_BLUETOOTH
      mReceiver = new BluetoothReceiver( mDeviceId, this );
#endif
    }
  }

  // Reset the position information to insure no cross contamination between receiver types
  lastGnssPositionInformationChanged( GnssPositionInformation() );
  connect( mReceiver, &AbstractGnssReceiver::lastGnssPositionInformationChanged, this, &Positioning::lastGnssPositionInformationChanged );
  setValid( mReceiver->valid() );

  emit deviceChanged();

  if ( mLogging )
  {
    mReceiver->startLogging();
  }

  if ( mActive )
  {
    mReceiver->connectDevice();
  }

  return;
}

void Positioning::lastGnssPositionInformationChanged( const GnssPositionInformation &lastGnssPositionInformation )
{
  if ( mPositionInformation == lastGnssPositionInformation )
    return;

  if ( mAveragedPosition )
  {
    mCollectedPositionInformations << lastGnssPositionInformation;
    mPositionInformation = PositioningUtils::averagedPositionInformation( mCollectedPositionInformations );
    emit averagedPositionCountChanged();
  }
  else
  {
    mPositionInformation = lastGnssPositionInformation;
  }

  if ( mPositionInformation.isValid() )
  {
    mSourcePosition = QgsPoint( mPositionInformation.longitude(), mPositionInformation.latitude(), mPositionInformation.elevation() );
  }
  else
  {
    mSourcePosition.clear();
  }

  if ( mCoordinateTransformer )
  {
    mCoordinateTransformer->setSourcePosition( mSourcePosition );
  }

  emit positionInformationChanged();
}

QgsPoint Positioning::sourcePosition() const
{
  return mSourcePosition;
}

QgsPoint Positioning::projectedPosition() const
{
  return mProjectedPosition;
}

double Positioning::projectedHorizontalAccuracy() const
{
  return mProjectedHorizontalAccuracy;
}

void Positioning::projectedPositionTransformed()
{
  mProjectedPosition = mCoordinateTransformer->projectedPosition();
  mProjectedHorizontalAccuracy = mPositionInformation.hacc();
  if ( mPositionInformation.haccValid() )
  {
    if ( mCoordinateTransformer->destinationCrs().mapUnits() != Qgis::DistanceUnit::Unknown )
    {
      mProjectedHorizontalAccuracy *= QgsUnitTypes::fromUnitToUnitFactor( Qgis::DistanceUnit::Meters,
                                                                          mCoordinateTransformer->destinationCrs().mapUnits() );
    }
    else
    {
      mProjectedHorizontalAccuracy = 0.0;
    }
  }

  emit projectedPositionChanged();
}
