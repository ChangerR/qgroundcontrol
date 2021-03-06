/****************************************************************************
 *
 *   (c) 2009-2016 QGROUNDCONTROL PROJECT <http://www.qgroundcontrol.org>
 *
 * QGroundControl is licensed according to the terms in the file
 * COPYING.md in the root of the source code directory.
 *
 ****************************************************************************/

#include "MissionSettingsComplexItem.h"
#include "JsonHelper.h"
#include "MissionController.h"
#include "QGCGeo.h"
#include "QGroundControlQmlGlobal.h"
#include "SimpleMissionItem.h"
#include "SettingsManager.h"
#include "AppSettings.h"

#include <QPolygonF>

QGC_LOGGING_CATEGORY(MissionSettingsComplexItemLog, "MissionSettingsComplexItemLog")

const char* MissionSettingsComplexItem::jsonComplexItemTypeValue = "MissionSettings";

const char* MissionSettingsComplexItem::_plannedHomePositionLatitudeName =  "PlannedHomePositionLatitude";
const char* MissionSettingsComplexItem::_plannedHomePositionLongitudeName = "PlannedHomePositionLongitude";
const char* MissionSettingsComplexItem::_plannedHomePositionAltitudeName =  "PlannedHomePositionAltitude";
const char* MissionSettingsComplexItem::_missionFlightSpeedName =           "FlightSpeed";
const char* MissionSettingsComplexItem::_missionEndActionName =             "MissionEndAction";

QMap<QString, FactMetaData*> MissionSettingsComplexItem::_metaDataMap;

MissionSettingsComplexItem::MissionSettingsComplexItem(Vehicle* vehicle, QObject* parent)
    : ComplexMissionItem(vehicle, parent)
    , _specifyMissionFlightSpeed(false)
    , _plannedHomePositionLatitudeFact  (0, _plannedHomePositionLatitudeName,   FactMetaData::valueTypeDouble)
    , _plannedHomePositionLongitudeFact (0, _plannedHomePositionLongitudeName,  FactMetaData::valueTypeDouble)
    , _plannedHomePositionAltitudeFact  (0, _plannedHomePositionAltitudeName,   FactMetaData::valueTypeDouble)
    , _missionFlightSpeedFact           (0, _missionFlightSpeedName,            FactMetaData::valueTypeDouble)
    , _missionEndActionFact             (0, _missionEndActionName,              FactMetaData::valueTypeUint32)
    , _sequenceNumber(0)
    , _dirty(false)
{
    _editorQml = "qrc:/qml/MissionSettingsEditor.qml";

    if (_metaDataMap.isEmpty()) {
        _metaDataMap = FactMetaData::createMapFromJsonFile(QStringLiteral(":/json/MissionSettings.FactMetaData.json"), NULL /* metaDataParent */);
    }

    _plannedHomePositionLatitudeFact.setMetaData    (_metaDataMap[_plannedHomePositionLatitudeName]);
    _plannedHomePositionLongitudeFact.setMetaData   (_metaDataMap[_plannedHomePositionLongitudeName]);
    _plannedHomePositionAltitudeFact.setMetaData    (_metaDataMap[_plannedHomePositionAltitudeName]);
    _missionFlightSpeedFact.setMetaData             (_metaDataMap[_missionFlightSpeedName]);
    _missionEndActionFact.setMetaData               (_metaDataMap[_missionEndActionName]);

    _plannedHomePositionLatitudeFact.setRawValue    (_plannedHomePositionLatitudeFact.rawDefaultValue());
    _plannedHomePositionLongitudeFact.setRawValue   (_plannedHomePositionLongitudeFact.rawDefaultValue());
    _plannedHomePositionAltitudeFact.setRawValue    (_plannedHomePositionAltitudeFact.rawDefaultValue());
    _missionEndActionFact.setRawValue               (_missionEndActionFact.rawDefaultValue());

    // FIXME: Flight speed default value correctly based firmware parameter if online
    AppSettings* appSettings = qgcApp()->toolbox()->settingsManager()->appSettings();
    Fact* speedFact = vehicle->multiRotor() ? appSettings->offlineEditingHoverSpeed() : appSettings->offlineEditingCruiseSpeed();
    _missionFlightSpeedFact.setRawValue(speedFact->rawValue().toDouble());

    setHomePositionSpecialCase(true);

    connect(this,               &MissionSettingsComplexItem::specifyMissionFlightSpeedChanged,  this, &MissionSettingsComplexItem::_setDirtyAndUpdateLastSequenceNumber);
    connect(&_cameraSection,    &CameraSection::missionItemCountChanged,                        this, &MissionSettingsComplexItem::_setDirtyAndUpdateLastSequenceNumber);

    connect(&_plannedHomePositionLatitudeFact,  &Fact::valueChanged, this, &MissionSettingsComplexItem::_setDirtyAndUpdateCoordinate);
    connect(&_plannedHomePositionLongitudeFact, &Fact::valueChanged, this, &MissionSettingsComplexItem::_setDirtyAndUpdateCoordinate);
    connect(&_plannedHomePositionAltitudeFact,  &Fact::valueChanged, this, &MissionSettingsComplexItem::_setDirtyAndUpdateCoordinate);

    connect(&_missionFlightSpeedFact,           &Fact::valueChanged, this, &MissionSettingsComplexItem::_setDirty);
    connect(&_missionEndActionFact,             &Fact::valueChanged, this, &MissionSettingsComplexItem::_setDirty);

    connect(&_cameraSection, &CameraSection::dirtyChanged, this, &MissionSettingsComplexItem::_cameraSectionDirtyChanged);
}


void MissionSettingsComplexItem::setSpecifyMissionFlightSpeed(bool specifyMissionFlightSpeed)
{
    if (specifyMissionFlightSpeed != _specifyMissionFlightSpeed) {
        _specifyMissionFlightSpeed = specifyMissionFlightSpeed;
        emit specifyMissionFlightSpeedChanged(specifyMissionFlightSpeed);
    }
}

int MissionSettingsComplexItem::lastSequenceNumber(void) const
{
    int lastSequenceNumber = _sequenceNumber + 1;   // +1 for planned home position

    if (_specifyMissionFlightSpeed) {
        lastSequenceNumber++;
    }
    lastSequenceNumber += _cameraSection.missionItemCount();

    return lastSequenceNumber;
}

void MissionSettingsComplexItem::setDirty(bool dirty)
{
    if (_dirty != dirty) {
        _dirty = dirty;
        emit dirtyChanged(_dirty);
    }
}

void MissionSettingsComplexItem::save(QJsonArray&  missionItems)
{
    QList<MissionItem*> items;

    appendMissionItems(items, this);

    // First item show be planned home position, we are not reponsible for save/load
    // Remained we just output as is
    for (int i=1; i<items.count(); i++) {
        MissionItem* item = items[i];
        QJsonObject saveObject;
        item->save(saveObject);
        missionItems.append(saveObject);
        item->deleteLater();
    }
}

void MissionSettingsComplexItem::setSequenceNumber(int sequenceNumber)
{
    if (_sequenceNumber != sequenceNumber) {
        _sequenceNumber = sequenceNumber;
        emit sequenceNumberChanged(sequenceNumber);
        emit lastSequenceNumberChanged(lastSequenceNumber());
    }
}

bool MissionSettingsComplexItem::load(const QJsonObject& complexObject, int sequenceNumber, QString& errorString)
{
    Q_UNUSED(complexObject);
    Q_UNUSED(sequenceNumber);
    Q_UNUSED(errorString);

    return true;
}

double MissionSettingsComplexItem::greatestDistanceTo(const QGeoCoordinate &other) const
{
    Q_UNUSED(other);
    return 0;
}

bool MissionSettingsComplexItem::specifiesCoordinate(void) const
{
    return false;
}

void MissionSettingsComplexItem::appendMissionItems(QList<MissionItem*>& items, QObject* missionItemParent)
{
    int seqNum = _sequenceNumber;

    // IMPORTANT NOTE: If anything changes here you must also change MissionSettingsComplexItem::scanForMissionSettings

    // Planned home position
    MissionItem* item = new MissionItem(seqNum++,
                                        MAV_CMD_NAV_WAYPOINT,
                                        MAV_FRAME_GLOBAL,
                                        0,                      // Hold time
                                        0,                      // Acceptance radius
                                        0,                      // Not sure?
                                        0,                      // Yaw
                                        _plannedHomePositionLatitudeFact.rawValue().toDouble(),
                                        _plannedHomePositionLongitudeFact.rawValue().toDouble(),
                                        _plannedHomePositionAltitudeFact.rawValue().toDouble(),
                                        true,                   // autoContinue
                                        false,                  // isCurrentItem
                                        missionItemParent);
    items.append(item);

    if (_specifyMissionFlightSpeed) {
        qDebug() << _missionFlightSpeedFact.rawValue().toDouble();
        MissionItem* item = new MissionItem(seqNum++,
                                            MAV_CMD_DO_CHANGE_SPEED,
                                            MAV_FRAME_MISSION,
                                            _vehicle->multiRotor() ? 1 /* groundspeed */ : 0 /* airspeed */,    // Change airspeed or groundspeed
                                            _missionFlightSpeedFact.rawValue().toDouble(),
                                            -1,                                                                 // No throttle change
                                            0,                                                                  // Absolute speed change
                                            0, 0, 0,                                                            // param 5-7 not used
                                            true,                                                               // autoContinue
                                            false,                                                              // isCurrentItem
                                            missionItemParent);
        items.append(item);
    }

    _cameraSection.appendMissionItems(items, missionItemParent, seqNum);
}

bool MissionSettingsComplexItem::scanForMissionSettings(QmlObjectListModel* visualItems, int scanIndex, Vehicle* vehicle)
{
    bool foundSpeed = false;
    bool foundCameraSection = false;
    bool stopLooking = false;

    qCDebug(MissionSettingsComplexItemLog) << "MissionSettingsComplexItem::scanForMissionSettings count:scanIndex" << visualItems->count() << scanIndex;

    MissionSettingsComplexItem* settingsItem = visualItems->value<MissionSettingsComplexItem*>(scanIndex);
    if (!settingsItem) {
        qWarning() << "Item specified by scanIndex not MissionSettingsComplexItem";
        return false;
    }

    // Scan through the initial mission items for possible mission settings

    scanIndex++;
    while (!stopLooking && visualItems->count() > 1) {
        SimpleMissionItem* item = visualItems->value<SimpleMissionItem*>(scanIndex);
        if (!item) {
            // We hit a complex item, there can be no more possible mission settings
            break;
        }
        MissionItem& missionItem = item->missionItem();

        qCDebug(MissionSettingsComplexItemLog) << item->command() << missionItem.param1() << missionItem.param2() << missionItem.param3() << missionItem.param4() << missionItem.param5() << missionItem.param6() << missionItem.param7() ;

        // See MissionSettingsComplexItem::getMissionItems for specs on what compomises a known mission setting

        switch ((MAV_CMD)item->command()) {
        case MAV_CMD_DO_CHANGE_SPEED:
            if (!foundSpeed && missionItem.param3() == -1 && missionItem.param4() == 0 && missionItem.param5() == 0 && missionItem.param6() == 0 && missionItem.param7() == 0) {
                if (vehicle->multiRotor()) {
                    if (missionItem.param1() != 1) {
                        stopLooking = true;
                        break;
                    }
                } else {
                    if (missionItem.param1() != 0) {
                        stopLooking = true;
                        break;
                    }
                }
                foundSpeed = true;
                settingsItem->setSpecifyMissionFlightSpeed(true);
                settingsItem->missionFlightSpeed()->setRawValue(missionItem.param2());
                visualItems->removeAt(scanIndex)->deleteLater();
                continue;
            }
            stopLooking = true;
            break;

        default:
            if (!foundCameraSection) {
                if (settingsItem->_cameraSection.scanForCameraSection(visualItems, scanIndex)) {
                    foundCameraSection = true;
                    continue;
                }
            }
            stopLooking = true;
            break;
        }
    }

    return foundSpeed || foundCameraSection;
}

double MissionSettingsComplexItem::complexDistance(void) const
{
    return 0;
}

void MissionSettingsComplexItem::setCruiseSpeed(double cruiseSpeed)
{
    // We don't care about cruise speed
    Q_UNUSED(cruiseSpeed);
}

void MissionSettingsComplexItem::_setDirty(void)
{
    setDirty(true);
}

void MissionSettingsComplexItem::setCoordinate(const QGeoCoordinate& coordinate)
{
    if (this->coordinate() != coordinate) {
        _plannedHomePositionLatitudeFact.setRawValue(coordinate.latitude());
        _plannedHomePositionLongitudeFact.setRawValue(coordinate.longitude());
        _plannedHomePositionAltitudeFact.setRawValue(coordinate.altitude());
    }
}

void MissionSettingsComplexItem::_setDirtyAndUpdateLastSequenceNumber(void)
{
    emit lastSequenceNumberChanged(lastSequenceNumber());
    setDirty(true);
}

void MissionSettingsComplexItem::_setDirtyAndUpdateCoordinate(void)
{
    emit coordinateChanged(coordinate());
    emit exitCoordinateChanged(coordinate());
    setDirty(true);
}

QGeoCoordinate MissionSettingsComplexItem::coordinate(void) const
{
    return QGeoCoordinate(_plannedHomePositionLatitudeFact.rawValue().toDouble(), _plannedHomePositionLongitudeFact.rawValue().toDouble(), _plannedHomePositionAltitudeFact.rawValue().toDouble());
}

void MissionSettingsComplexItem::_cameraSectionDirtyChanged(bool dirty)
{
    if (dirty) {
        setDirty(true);
    }
}
