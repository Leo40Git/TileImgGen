#include "tilegen.h"
#include "ui_tilegen.h"
#include <QFileDialog>
#include <QJsonDocument>
#include <QStringLiteral>
#include <QMessageBox>
#include <QJsonObject>
#include <QJsonArray>
#include <QJsonValue>
#include <QDebug>
#include <QPainter>

TileGen::TileGen(QWidget *parent) :
    QMainWindow(parent),
    ui(new Ui::TileGen)
{
    ui->setupUi(this);
}

TileGen::~TileGen()
{
    delete ui;
}

void TileGen::print(const QString &message) {
    qInfo() << message.toUtf8();
    ui->pteOut->appendHtml(message);
}

void TileGen::error(const QString &message, bool msgBox) {
    qCritical() << message.toUtf8();
    ui->pteOut->appendHtml(QStringLiteral("<p style=\"color:red;\">%0</p>").arg(message));
    if (msgBox)
        QMessageBox::critical(this, "Error!", QString("An error has occured:\n%0").arg(message));
}

void TileGen::on_btnParse_clicked()
{
    ui->pteOut->clear();
    error("New parse session!");
    const QString& imgFilter = "Image files (*.png *.bmp)";
    const QString& baseImgN = QFileDialog::getOpenFileName(this, "Open base image", "", imgFilter);
    if (baseImgN.isEmpty())
        return;
    baseImg = QImage(baseImgN);
    if (baseImg.isNull()) {
        error(QString("Failed to load base image \"%0\"!").arg(baseImgN), true);
        return;
    }
    print(QString("Loaded base image \"%0\"").arg(baseImgN));
    const QString& txtFilter = "JSON files (*.json)";
    const QString& layerFileN = QFileDialog::getOpenFileName(this, "Open layer scheme", "", txtFilter);
    if (layerFileN.isEmpty())
        return;
    QFile layerFile(layerFileN);
    if (!layerFile.open(QIODevice::ReadOnly | QIODevice::Text)) {
        error(QString("Failed to open layer scheme file \"%0\"!").arg(layerFileN), true);
        return;
    }
    print(QString("Reading layer scheme from file \"%0\"").arg(layerFileN));
    const QString& tileFileN = QFileDialog::getOpenFileName(this, "Open tile scheme", "", txtFilter);
    if (tileFileN.isEmpty())
        return;
    QFile tileFile(tileFileN);
    if (!tileFile.open(QIODevice::ReadOnly | QIODevice::Text)) {
        error(QString("Failed to open tile scheme file \"%0\"!").arg(tileFileN), true);
        return;
    }
    const QString& imgOutN = QFileDialog::getSaveFileName(this, "Save output image", "", imgFilter);
    if (imgOutN.isEmpty())
        return;
    QFile imgOut(imgOutN);
    if (!imgOut.open(QIODevice::WriteOnly)) {
        error(QString("Failed to open image output \"%0\" for writing!").arg(tileFileN), true);
        return;
    }
    print(QString("Reading tile scheme from file \"%0\"").arg(tileFileN));
    QString layerData = layerFile.readAll();
    layerFile.close();
    QString tileData = tileFile.readAll();
    tileFile.close();
    QJsonDocument doc;
    QJsonParseError pErr;
    QString err;
    print("Now parsing layer scheme JSON...");
    doc = QJsonDocument::fromJson(layerData.toUtf8(), &pErr);
    if (pErr.error != QJsonParseError::NoError) {
        error("Parse fail!");
        error(QString("Could not parse layer scheme JSON: %0").arg(pErr.errorString()), true);
        return;
    }
    if (doc.isNull() || !doc.isObject()) {
        error("Parse fail!");
        error("Layer scheme JSON parse returned null/non-object root element", true);
        return;
    }
    if (!readLayerScheme(doc.object(), &err)) {
        error("Parse fail!");
        error(err, true);
        return;
    }
    print("Parse success!");
    print("Now parsing tile scheme JSON...");
    doc = QJsonDocument::fromJson(tileData.toUtf8(), &pErr);
    if (pErr.error != QJsonParseError::NoError) {
        error("Parse fail!");
        error(QString("Could not parse tile scheme JSON: %0").arg(pErr.errorString()), true);
        return;
    }
    if (doc.isNull() || !doc.isObject()) {
        error("Parse fail!");
        error("Tile scheme JSON parse returned null/non-object root element", true);
        return;
    }
    if (!readTileScheme(doc.object(), &err)) {
        error("Parse fail!");
        error(err, true);
        return;
    }
    print("Parse success!");
    print("Generating layer rects");
    if (!createLayerRects(&err)) {
        error("Generate fail!");
        error(err, true);
    }
    print("Generating tiles image");
    if (!generateTilesImage(&err)) {
        error("Generate fail!");
        error(err, true);
    }
    print("Generate successful!");
    print("Outputting tiles image");
    if (!tilesImg.save(&imgOut)) {
        error("Could not save tiles image!", true);
    }
    print("DONE!");
}

QString quotedOrUndefined(const QJsonObject& json, const QString& key) {
    return (json.contains(key) ? '"' + json[key].toString() + '"' : "undefined");
}

bool TileGen::readLayerScheme(const QJsonObject& json, QString* err) {
    if (json["SchemeType"].toString() != "Layer") {
        *err = "Bad SchemeType, should be \"Layer\" but is " + quotedOrUndefined(json, "SchemeType");
        return false;
    }
    layerDef.name = json["SchemeName"].toString();
    if (layerDef.name.isEmpty()) {
        *err = "SchemeName is empty or undefined";
        return false;
    }
    layerDef.tileSize = json["TileSize"].toInt(-1);
    if (layerDef.tileSize <= 0) {
        *err = "TileSize is 0, negative, not whole or undefined";
        return false;
    }
    layerDef.tilesPerRow = json["TilesPerRow"].toInt(-1);
    if (layerDef.tilesPerRow <= 0) {
        *err = "TilesPerRow is 0, negative, not whole or undefined";
        return false;
    }
    if (!json.contains("Layers") || !json["Layers"].isArray()) {
        *err = "Layers is either undefined or not an array";
        return false;
    }
    const QJsonArray& layersArr = json["Layers"].toArray();
    for (int i = 0; i < layersArr.size(); i++) {
        if (!layersArr[i].isObject()) {
            *err = "Layers: element " + QString("%0").arg(i) + " is not an object";
            return false;
        }
        const QJsonObject& layerObj = layersArr[i].toObject();
        QString errStart = "Layers[" + QString("%1").arg(i) + "]: ";
        const QString& layerKey = layerObj["Name"].toString();
        if (layerKey.isEmpty()) {
            *err = errStart + "Name is empty or undefined";
            return false;
        }
        Layer layer;
        layer.name = layerKey;
        layer.zOrder = i;
        layer.allowNull = layerObj["AllowNull"].toBool(true);
        if (!layerObj.contains("Values") || !layerObj["Values"].isArray()) {
            *err = errStart + "Values is either undefined or not an array";
            return false;
        }
        layer.values = QStringList();
        const QJsonArray& layerValsArr = layerObj["Values"].toArray();
        for (int i = 0; i < layerValsArr.size(); i++) {
            const QString& layerVal = layerValsArr[i].toString();
            if (layerVal.isEmpty())
                continue;
            layer.values.append(layerVal);
        }
        if (layer.values.size() == 0) {
            *err = errStart + "Values doesn't contain any values";
            return false;
        }
        const QString& defVal = layerObj["DefaultValue"].toString();
        if (defVal.isEmpty() && !layer.allowNull) {
            *err = errStart + "DefaultValue is undefined (null) but AllowNull is false";
            return false;
        }
        bool checkDefVal = true;
        if (layer.allowNull && defVal.isEmpty())
            checkDefVal = false;
        if (checkDefVal && !layer.values.contains(defVal)) {
            *err = errStart + "DefaultValue isn't an element of Values";
            return false;
        }
        layer.defaultValue = defVal;
        layerDef.layers[layerKey] = layer;
        layerDef.layersOrdered.append(layer);
    }
    // create default tile
    foreach (QString layer, layerDef.layers.keys())
        layerDef.defaultTile[layer] = layerDef.layers[layer].defaultValue;
    return true;
}

bool TileGen::readTileScheme(const QJsonObject& json, QString* err) {
    if (json["SchemeType"].toString() != "Tile") {
        *err = "Bad SchemeType, should be \"Tile\" but is " + quotedOrUndefined(json, "SchemeType");
        return false;
    }
    const QString& layerDefName = json["LayerSchemeName"].toString();
    if (layerDefName.isEmpty()) {
        *err = "LayerSchemeName is empty or undefined";
        return false;
    }
    if (layerDefName != layerDef.name) {
        *err = "Bad LayerSchemeName, should be \"" + layerDef.name + "\" but is \"" + layerDefName + "\"";
        return false;
    }
    int tileCount = -1;
    if (json.contains("TileCount") && json["TileCount"].isDouble()) {
        tileCount = json["TileCount"].toInt(-1);
        if (tileCount <= 0) {
            *err = "TileCount is 0, negative or not whole";
            return false;
        }
    }
    for (int i = 0; i < tileCount; i++)
        tiles.append(Tile(layerDef.defaultTile));
    if (!json.contains("Tiles") || !json["Tiles"].isArray()) {
        *err = "Tiles is either undefined or not an array";
        return false;
    }
    const QJsonArray& tilesArr = json["Tiles"].toArray();
    for (int i = 0; i < tilesArr.size(); i++) {
        if (!tilesArr[i].isObject()) {
            *err = "Tiles: element " + QString("%0").arg(i) + " is not an object";
            return false;
        }
        const QJsonObject& tileDef = tilesArr[i].toObject();
        QString errStart = "Tiles[" + QString("%0").arg(i) + "]: ";
        int start, end;
        if (tileDef.contains("Start") && tileDef["Start"].isDouble()) {
            start = tileDef["Start"].toInt(-1);
            if (start < 0) {
                *err = errStart + "Start is negative or not whole";
                return false;
            }
            if (tileDef.contains("End") && tileDef["End"].isDouble()) {
                end = tileDef["End"].toInt(-1);
                if (end < 0) {
                    *err = errStart + "End is negative or not whole";
                    return false;
                }
                if (end < start)
                    std::swap(start, end);
            } else
                end = start;
        } else {
            *err = errStart + "Start is either undefined or not a number";
            return false;
        }
        if (!tileDef["LayerValues"].isObject()) {
            *err = errStart + "LayerValues is either undefined or not an object";
            return false;
        }
        const QJsonObject& layerVals = tileDef["LayerValues"].toObject();
        errStart = "Tiles[" + QString("%0").arg(i) + "].LayerValues: ";
        Tile tile = Tile(layerDef.defaultTile);
        foreach (QString layerKey, layerVals.keys()) {
            qInfo() << "layerkey" << layerKey;
            if (!layerDef.layers.contains(layerKey)) {
                *err = errStart + "Has value for unknown layer \"" + layerKey + "\"";
                return false;
            }
            Layer* layer = &layerDef.layers[layerKey];
            const QString& layerVal = layerVals[layerKey].toString();
            bool checkVal = true;
            if (layerVal.isEmpty()) {
                if (layer->allowNull)
                    checkVal = false;
                else {
                    *err = errStart + "Value for layer \"" + layerKey + "\" is null, but layer doesn't allow null";
                    return false;
                }
            }
            if (checkVal && !layer->values.contains(layerVal)) {
                *err = errStart + "Value for layer \"" + layerKey + "\" is invalid";
                return false;
            }
            tile[layerKey] = layerVal;
        }
        // dump tile
        foreach (QString key, tile.keys())
            qInfo() << key << tile[key];
        for (int i = start; i < end + 1; i++)
            tiles[i] = tile;
    }
    if (tiles.size() != tileCount) {
        *err = QString("Generated tile count (%0) does not match TileCount (%1)").arg(tiles.size()).arg(tileCount);
        return false;
    }
    return true;
}

bool TileGen::createLayerRects(QString* err) {
    const int tileSize = layerDef.tileSize;
    const int maxW = baseImg.width() / layerDef.tileSize - 1;
    const int maxH = baseImg.height() / layerDef.tileSize - 1;
    int w = 0, h = 0;
    qInfo() << "maxW =" << maxW;
    qInfo() << "maxH =" << maxH;
    for (int i = 0; i < layerDef.layersOrdered.size(); i++) {
        if (h > maxH) {
            *err = "Image too small, went over maximum Y value";
            return false;
        }
        Layer* layer = &layerDef.layersOrdered[i];
        QString layerKey = layer->name;
        qInfo() << "layer" << layerKey;
        foreach (QString value, layer->values) {
            if (h > maxH) {
                *err = "Image too small, went over maximum Y value";
                return false;
            }
            qInfo() << " value" << value;
            layer->valueRects[value] = { w * tileSize, h * tileSize, tileSize, tileSize };
            qInfo() << " rect" << layer->valueRects[value];
            w++;
            if (w > maxW) {
                w = 0;
                h++;
            }
        }
        w = 0;
        h++;
    }
    // dump rects
    for (int i = 0; i < layerDef.layersOrdered.size(); i++) {
        Layer l = layerDef.layersOrdered[i];
        qInfo() << "Dumping rects for layer" << l.name;
        foreach (QString key, l.valueRects.keys()) {
            qInfo() << key << l.valueRects[key];
        }
    }
    return true;
}

bool TileGen::generateTilesImage(QString* err) {
    const int tileSize = layerDef.tilesPerRow;
    const int maxW = layerDef.tilesPerRow - 1;
    const int maxH = tiles.size() / maxW - 1;
    tilesImg = QImage((maxW + 1) * tileSize, maxH * tileSize, QImage::Format_ARGB32);
    tilesImg.fill(qRgba(0, 0, 0, 0));
    QPainter tp(&tilesImg);
    int w = 0, h = 0;
    for (int i = 0; i < layerDef.layersOrdered.size(); i++) {
        Layer* l = &layerDef.layersOrdered[i];
        qInfo() << "layer" << l->name;
        for (int j = 0; j < tiles.size(); j++) {
            if (h > maxH) {
                *err = "Calculation error??? Went over maximum Y value";
                return false;
            }
            Tile t = tiles[j];
            qInfo() << " tile" << j;
            const QPoint& p = { w * tileSize, h * tileSize };
            qInfo() << " pos" << p;
            const QString& lv = t[l->name];
            qInfo() << " value" << lv;
            qInfo() << " rect" << l->valueRects[lv];
            if (!lv.isEmpty())
                tp.drawImage(p, baseImg, l->valueRects[lv]);
            w++;
            if (w > maxW) {
                w = 0;
                h++;
            }
        }
        w = 0;
        h = 0;
    }
    tp.end();
    return true;
}
