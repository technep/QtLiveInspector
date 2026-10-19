#pragma once

#include <QString>
#include <QStringList>

class QObject;

// Generates Squish-style (froglogic) object locators for a QObject.
//
// Squish identifies Qt objects with multi-property names such as:
//   {container=':mainWindow' objectName='okButton' type='QPushButton' visible='1'}
// Unnamed objects are addressed by type plus the 1-based occurrence index
// among same-type unnamed siblings:
//   {container=':mainWindow' type='Rectangle' occurrence='2' visible='true'}
namespace SquishLocator {

// The full locator for obj (last element of the ancestor chain).
QString locatorFor(QObject *obj);

// One locator element per ancestor, from the top-level object down to obj.
QStringList chainFor(QObject *obj);

} // namespace SquishLocator
