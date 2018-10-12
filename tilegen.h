#ifndef TILEGEN_H
#define TILEGEN_H

#include <QMainWindow>
#include <QMap>
#include <QStringList>

namespace Ui {
class TileGen;
}

typedef struct {
    QString name;
    int zOrder;
    bool allowNull;
    QStringList values;
    QString defaultValue;
    QMap<QString, QRect> valueRects;
} Layer;

typedef QMap<QString, QString> Tile;

typedef struct {
    QString name;
    int tileSize;
    int tilesPerRow;
    QMap<QString, Layer> layers;
    QList<Layer> layersOrdered;
    Tile defaultTile;
} LayerDefinition;

class TileGen : public QMainWindow
{
    Q_OBJECT

public:
    explicit TileGen(QWidget *parent = nullptr);
    ~TileGen();

private:
    void print(const QString &message);
    void error(const QString &message, bool msgBox = false);
    bool readLayerScheme(const QJsonObject& json, QString* err);
    bool readTileScheme(const QJsonObject& json, QString* err);
    bool createLayerRects(QString* err);
    bool generateTilesImage(QString* err);
    QPixmap baseImg;
    LayerDefinition layerDef;
    QList<Tile> tiles;
    QImage tilesImg;

private slots:
    void on_btnParse_clicked();

private:
    Ui::TileGen *ui;
};

#endif // TILEGEN_H
