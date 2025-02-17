/***************************************************************************
    qgssggeometry.cpp
    ---------------------
    begin                : March 2020
    copyright            : (C) 2020 by David Signer
    email                : david at opengis dot ch
 ***************************************************************************
 *                                                                         *
 *   This program is free software; you can redistribute it and/or modify  *
 *   it under the terms of the GNU General Public License as published by  *
 *   the Free Software Foundation; either version 2 of the License, or     *
 *   (at your option) any later version.                                   *
 *                                                                         *
 ***************************************************************************/

#include "qgssggeometry.h"

#include <qgscurve.h>
#include <qgsgeometry.h>
#include <qgspolygon.h>
#include <qgstessellator.h>

#include <math.h>

QgsSGGeometry::QgsSGGeometry()
{
}

QgsSGGeometry::QgsSGGeometry( const QgsGeometry &geom, const QColor &color, float width, const QgsRectangle visibleExtent, double scaleFactor )
{
  //TODO: Fix const-correcteness upstream
  QgsGeometry &gg = const_cast<QgsGeometry &>( geom );

  mMaterial.setColor( color );

  switch ( gg.type() )
  {
    case Qgis::GeometryType::Point:
      // we should never get point here, use GeometryRenderer quick item to render geometries
      Q_ASSERT( false );
      break;

    case Qgis::GeometryType::Line:
      if ( gg.isMultipart() )
      {
        const QgsMultiPolylineXY lines = gg.asMultiPolyline();

        for ( const QgsPolylineXY &line : lines )
        {
          QSGGeometryNode *geomNode = new QSGGeometryNode;
          geomNode->setGeometry( qgsPolylineToQSGGeometry( line, width, visibleExtent, scaleFactor ) );
          geomNode->setFlag( QSGNode::OwnsGeometry );
          applyStyle( geomNode );
          appendChildNode( geomNode );
        }
      }
      else
      {
        QSGGeometryNode *geomNode = new QSGGeometryNode;
        geomNode->setGeometry( qgsPolylineToQSGGeometry( gg.asPolyline(), width, visibleExtent, scaleFactor ) );
        geomNode->setFlag( QSGNode::OwnsGeometry );
        applyStyle( geomNode );
        appendChildNode( geomNode );
      }
      break;

    case Qgis::GeometryType::Polygon:
    {
      QgsGeometry pg( gg );
      if ( !pg.isGeosValid() )
      {
        pg = pg.makeValid();
      }

      auto addPolygon = [=]( QgsCurvePolygon *curvePolygon, QSGOpacityNode *node ) {
        if ( !curvePolygon )
          return;

        std::unique_ptr<QgsPolygon> polygon( qgsgeometry_cast<QgsPolygon *>( curvePolygon->segmentize() ) );
        if ( !polygon )
          return;

        // Area
        QSGGeometryNode *geomNode = new QSGGeometryNode;
        geomNode->setGeometry( qgsPolygonToQSGGeometry( polygon.get(), visibleExtent, scaleFactor ) );
        geomNode->setFlag( QSGNode::OwnsGeometry );
        applyStyle( geomNode );
        node->appendChildNode( geomNode );

        // Interior outline
        for ( int i = 0; i < polygon->numInteriorRings(); i++ )
        {
          QgsGeometry interior( polygon->interiorRing( i )->clone() );
          geomNode = new QSGGeometryNode;
          geomNode->setGeometry( qgsPolylineToQSGGeometry( interior.asPolyline(), width, visibleExtent, scaleFactor ) );
          geomNode->setFlag( QSGNode::OwnsGeometry );
          applyStyle( geomNode );
          appendChildNode( geomNode );
        }

        // Exterior outline
        QgsGeometry exterior( polygon->toCurveType()->exteriorRing() );
        geomNode = new QSGGeometryNode;
        geomNode->setGeometry( qgsPolylineToQSGGeometry( exterior.asPolyline(), width, visibleExtent, scaleFactor ) );
        geomNode->setFlag( QSGNode::OwnsGeometry );
        applyStyle( geomNode );
        appendChildNode( geomNode );
      };

      QSGOpacityNode *on = new QSGOpacityNode;
      on->setOpacity( 0.25 );

      if ( pg.isMultipart() )
      {
        QgsGeometryPartIterator it = pg.parts();
        while ( it.hasNext() )
        {
          QgsCurvePolygon *c = qgsgeometry_cast<QgsCurvePolygon *>( it.next() );
          addPolygon( c, on );
        }
      }
      else
      {
        QgsCurvePolygon *c = qgsgeometry_cast<QgsCurvePolygon *>( pg.get() );
        addPolygon( c, on );
      }

      appendChildNode( on );
      break;
    }

    case Qgis::GeometryType::Unknown:
    case Qgis::GeometryType::Null:
      break;
  }
}

void QgsSGGeometry::applyStyle( QSGGeometryNode *geomNode )
{
  geomNode->setMaterial( &mMaterial );
}

QSGGeometry *QgsSGGeometry::qgsPolylineToQSGGeometry( const QgsPolylineXY &line, float width, const QgsRectangle visibleExtent, double scaleFactor )
{
  QSGGeometry *sgGeom = new QSGGeometry( QSGGeometry::defaultAttributes_Point2D(), line.count() );
  QSGGeometry::Point2D *vertices = sgGeom->vertexDataAsPoint2D();

  int i = 0;
  for ( const QgsPointXY &point : line )
  {
    vertices[i].set( static_cast<float>( ( point.x() - visibleExtent.xMinimum() ) * scaleFactor ),
                     static_cast<float>( ( point.y() - visibleExtent.yMaximum() ) * -scaleFactor ) );
    i++;
  }

  sgGeom->setLineWidth( width );
  sgGeom->setDrawingMode( QSGGeometry::DrawLineStrip );

  return sgGeom;
}

QSGGeometry *QgsSGGeometry::qgsPolygonToQSGGeometry( const QgsPolygon *polygon, const QgsRectangle visibleExtent, double scaleFactor )
{
  Q_ASSERT( polygon );

  QgsGeometry geom( polygon->clone() );
  geom = geom.buffer( 0.0000001, 5 );
  QgsPolygon *bufferedPolygon = qgsgeometry_cast<QgsPolygon *>( geom.constGet() );
  QgsTessellator t( visibleExtent.xMinimum(), visibleExtent.yMaximum(), false, false, false, true );
  if ( bufferedPolygon )
  {
    t.addPolygon( *bufferedPolygon, 0 );
  }

  QSGGeometry *sgGeom = new QSGGeometry( QSGGeometry::defaultAttributes_Point2D(), t.dataVerticesCount() );

  QSGGeometry::Point2D *vertices = sgGeom->vertexDataAsPoint2D();

  const QVector<float> triangleData = t.data();
  int currentVertex = 0;
  for ( auto it = triangleData.constBegin(); it != triangleData.constEnd(); )
  {
    vertices[currentVertex].x = ( *it++ * scaleFactor );
    ( void ) it++; // z
    vertices[currentVertex].y = ( ( *it++ ) * scaleFactor );
    currentVertex++;

    vertices[currentVertex].x = ( *it++ * scaleFactor );
    ( void ) it++; // z
    vertices[currentVertex].y = ( ( *it++ * scaleFactor ) );
    currentVertex++;

    vertices[currentVertex].x = ( *it++ * scaleFactor );
    ( void ) it++; // z
    vertices[currentVertex].y = ( ( *it++ * scaleFactor ) );
    currentVertex++;
  }

  sgGeom->setDrawingMode( QSGGeometry::DrawTriangles );

  return sgGeom;
}
