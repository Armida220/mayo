/****************************************************************************
** Copyright (c) 2018, Fougue Ltd. <http://www.fougue.pro>
** All rights reserved.
** See license at https://github.com/fougue/mayo/blob/master/LICENSE.txt
****************************************************************************/

#include "widget_application_tree.h"

#include "application.h"
#include "application_item_selection_model.h"
#include "caf_utils.h"
#include "string_utils.h"
#include "document.h"
#include "document_item.h"
#include "gui_application.h"
#include "mesh_item.h"
#include "xde_document_item.h"

#include <QtCore/QMetaType>
#include <QtWidgets/QTreeWidget>
#include <QtWidgets/QTreeWidgetItemIterator>

#include <cassert>
#include <unordered_map>
#include <unordered_set>

Q_DECLARE_METATYPE(Mayo::XdeAssemblyNode)

namespace Mayo {
namespace Internal {

class TreeWidget : public QTreeWidget {
public:
    TreeWidget(QWidget* parent = nullptr)
        : QTreeWidget(parent)
    {}

    QModelIndex indexFromItem(QTreeWidgetItem* item, int column = 0) const {
        return QTreeWidget::indexFromItem(item, column);
    }

    QTreeWidgetItem* itemFromIndex(const QModelIndex &index) const {
        return QTreeWidget::itemFromIndex(index);
    }
};

} // namespace Internal
} // namespace Mayo
#include "ui_widget_application_tree.h"

namespace Mayo {

namespace Internal {

enum TreeItemRole {
    TreeItemTypeRole = Qt::UserRole + 1,
    TreeItemDocumentRole,
    TreeItemDocumentItemRole,
    TreeItemXdeAssemblyNodeRole
};

enum TreeItemType {
    TreeItemType_Unknown = 0,
    TreeItemType_Document = 1,
    TreeItemType_DocumentItem = 2,
    TreeItemType_XdeAssemblyNode = 3
};

template<typename T> T* qVariantToPtr(const QVariant& var) {
    return static_cast<T*>(var.value<void*>());
}

template<typename T> QVariant ptrToQVariant(T* ptr) {
    return qVariantFromValue(reinterpret_cast<void*>(ptr));
}

static TreeItemType treeItemType(const QTreeWidgetItem* treeItem)
{
    const QVariant varType = treeItem->data(0, TreeItemTypeRole);
    return varType.isValid() ?
                static_cast<TreeItemType>(varType.toInt()) :
                TreeItemType_Unknown;
}

static Document* treeItemDocument(const QTreeWidgetItem* treeItem)
{
    const QVariant var = treeItem->data(0, TreeItemDocumentRole);
    return var.isValid() ? qVariantToPtr<Document>(var) : nullptr;
}

static DocumentItem* treeItemDocumentItem(const QTreeWidgetItem* treeItem)
{
    const QVariant var = treeItem->data(0, TreeItemDocumentItemRole);
    return var.isValid() ? qVariantToPtr<DocumentItem>(var) : nullptr;
}

static XdeAssemblyNode treeItemXdeAssemblyNode(const QTreeWidgetItem* treeItem)
{
    const QVariant var = treeItem->data(0, TreeItemXdeAssemblyNodeRole);
    return var.isValid() ?
                var.value<XdeAssemblyNode>() : XdeAssemblyNode::null();
}

static void setTreeItemDocument(QTreeWidgetItem* treeItem, Document* doc)
{
    treeItem->setData(0, TreeItemTypeRole, TreeItemType_Document);
    treeItem->setData(0, TreeItemDocumentRole, ptrToQVariant(doc));
}

static void setTreeItemDocumentItem(QTreeWidgetItem* treeItem, DocumentItem* docItem)
{
    treeItem->setData(0, TreeItemTypeRole, TreeItemType_DocumentItem);
    treeItem->setData(0, TreeItemDocumentItemRole, ptrToQVariant(docItem));
}

static void setTreeItemXdeAssemblyNode(
        QTreeWidgetItem* treeItem, const XdeAssemblyNode& lbl)
{
    treeItem->setData(0, TreeItemTypeRole, TreeItemType_XdeAssemblyNode);
    treeItem->setData(0, TreeItemXdeAssemblyNodeRole, QVariant::fromValue(lbl));
}

static QIcon documentItemIcon(const DocumentItem* docItem)
{
    if (sameType<XdeDocumentItem>(docItem))
        return QIcon(":/images/xde_document_16.png");
    else if (sameType<MeshItem>(docItem))
        return QIcon(":/images/mesh_16.png");
    return QIcon();
}

static QIcon xdeShapeIcon(const XdeDocumentItem* docItem, const TDF_Label& label)
{
    if (docItem->isShapeAssembly(label))
        return QIcon(":/images/xde_assembly_16.png");
    else if (docItem->isShapeReference(label))
        return QIcon(":/images/xde_reference_16.png");
    else if (docItem->isShapeSimple(label))
        return QIcon(":/images/xde_simple_shape_16.png");
    return QIcon();
}

static QString documentItemLabel(const DocumentItem* docItem)
{
    const QString docItemLabel = docItem->propertyLabel.value();
    return !docItemLabel.isEmpty() ?
                docItemLabel :
                WidgetApplicationTree::tr("<unnamed>");
}

static void addValidationProperties(
        const XdeDocumentItem::ValidationProperties& validationProps,
        std::vector<HandleProperty>* ptrVecHndProp,
        const QString& nameFormat = QStringLiteral("%1"),
        HandleProperty::Storage hndStorage = HandleProperty::Owner,
        PropertyOwner* propOwner = nullptr)
{
    if (validationProps.hasCentroid) {
        auto propCentroid = new PropertyOccPnt(
                    propOwner, nameFormat.arg(WidgetApplicationTree::tr("Centroid")));
        propCentroid->setValue(validationProps.centroid);
        ptrVecHndProp->emplace_back(propCentroid, hndStorage);
    }
    if (validationProps.hasArea) {
        auto propArea = new PropertyArea(
                    propOwner, nameFormat.arg(WidgetApplicationTree::tr("Area")));
        propArea->setQuantity(validationProps.area);
        ptrVecHndProp->emplace_back(propArea, hndStorage);
    }
    if (validationProps.hasVolume) {
        auto propVolume = new PropertyVolume(
                    propOwner, nameFormat.arg(WidgetApplicationTree::tr("Volume")));
        propVolume->setQuantity(validationProps.volume);
        ptrVecHndProp->emplace_back(propVolume, hndStorage);
    }
}

static QTreeWidgetItem* guiCreateXdeTreeNode(
        QTreeWidgetItem* guiParentNode,
        XdeDocumentItem::AssemblyNodeId nodeId,
        XdeDocumentItem* xdeDocItem)
{
    auto guiNode = new QTreeWidgetItem(guiParentNode);
    const QString stdName = xdeDocItem->findLabelName(nodeId);
    guiNode->setText(0, stdName);
    setTreeItemXdeAssemblyNode(guiNode, XdeAssemblyNode(xdeDocItem, nodeId));
    const QIcon icon = xdeShapeIcon(
                xdeDocItem, xdeDocItem->assemblyTree().nodeData(nodeId));
    if (!icon.isNull())
        guiNode->setIcon(0, icon);
    return guiNode;
}

static ApplicationItem toApplicationItem(const QTreeWidgetItem* treeItem)
{
    const Internal::TreeItemType type = Internal::treeItemType(treeItem);
    if (type == Internal::TreeItemType_Document)
        return ApplicationItem(Internal::treeItemDocument(treeItem));
    else if (type == Internal::TreeItemType_DocumentItem)
        return ApplicationItem(Internal::treeItemDocumentItem(treeItem));
    else if (type == Internal::TreeItemType_XdeAssemblyNode)
        return ApplicationItem(Internal::treeItemXdeAssemblyNode(treeItem));
    return ApplicationItem();
}

} // namespace Internal

WidgetApplicationTree::WidgetApplicationTree(QWidget *widget)
    : QWidget(widget),
      m_ui(new Ui_WidgetApplicationTree)
{
    m_ui->setupUi(this);

    auto app = Application::instance();
    QObject::connect(
                app, &Application::documentAdded,
                this, &WidgetApplicationTree::onDocumentAdded);
    QObject::connect(
                app, &Application::documentErased,
                this, &WidgetApplicationTree::onDocumentErased);
    QObject::connect(
                app, &Application::documentItemAdded,
                this, &WidgetApplicationTree::onDocumentItemAdded);
    QObject::connect(
                app, &Application::documentItemPropertyChanged,
                this, &WidgetApplicationTree::onDocumentItemPropertyChanged);
    QObject::connect(
                m_ui->treeWidget_App->selectionModel(),
                &QItemSelectionModel::selectionChanged,
                this,
                &WidgetApplicationTree::onTreeWidgetDocumentSelectionChanged);
}

WidgetApplicationTree::~WidgetApplicationTree()
{
    delete m_ui;
}

bool WidgetApplicationTree::isMergeXdeReferredShapeOn() const
{
    return m_isMergeXdeReferredShapeOn;
}

void WidgetApplicationTree::setMergeXdeReferredShape(bool on)
{
    m_isMergeXdeReferredShapeOn = on;
    // TODO : reload XDE documents
}

void WidgetApplicationTree::onDocumentAdded(Document *doc)
{
    auto treeItem = new QTreeWidgetItem;
    const QString docLabel =
            !doc->label().isEmpty() ? doc->label() : tr("<unnamed>");
    treeItem->setText(0, docLabel);
    treeItem->setIcon(0, QPixmap(":/images/file_16.png"));
    treeItem->setToolTip(0, doc->filePath());
    Internal::setTreeItemDocument(treeItem, doc);
    assert(Internal::treeItemDocument(treeItem) == doc);
    m_ui->treeWidget_App->addTopLevelItem(treeItem);
}

void WidgetApplicationTree::onDocumentErased(const Document *doc)
{
    delete this->findTreeItemDocument(doc);
}

QTreeWidgetItem* WidgetApplicationTree::loadDocumentItem(DocumentItem* docItem)
{
    auto treeItem = new QTreeWidgetItem;
    const QString docItemLabel = Internal::documentItemLabel(docItem);
    treeItem->setText(0, docItemLabel);
    const QIcon docItemIcon = Internal::documentItemIcon(docItem);
    if (!docItemIcon.isNull())
        treeItem->setIcon(0, docItemIcon);
    Internal::setTreeItemDocumentItem(treeItem, docItem);
    return treeItem;
}

void WidgetApplicationTree::guiBuildXdeTree(
        QTreeWidgetItem *treeDocItem, XdeDocumentItem *xdeDocItem)
{
    std::unordered_map<TreeNodeId, QTreeWidgetItem*> mapNodeIdToTreeItem;
    std::unordered_set<TreeNodeId> setRefNodeId;
    const Tree<TDF_Label>& asmTree = xdeDocItem->assemblyTree();
    deepForeachTreeNode(asmTree, [&](TreeNodeId nodeId) {
        const TreeNodeId nodeParentId = asmTree.nodeParent(nodeId);
        const TDF_Label& nodeLabel = asmTree.nodeData(nodeId);
        auto itParentFound = mapNodeIdToTreeItem.find(nodeParentId);
        QTreeWidgetItem* guiParentNode =
                itParentFound != mapNodeIdToTreeItem.end() ?
                    itParentFound->second : treeDocItem;
        if (m_isMergeXdeReferredShapeOn) {
            if (xdeDocItem->isShapeReference(nodeLabel)) {
                mapNodeIdToTreeItem.insert({ nodeId, guiParentNode });
                setRefNodeId.insert(nodeId);
            }
            else {
                auto guiNode = new QTreeWidgetItem(guiParentNode);
                QString guiNodeText = xdeDocItem->findLabelName(nodeLabel);
                XdeDocumentItem::AssemblyNodeId guiNodeId = nodeId;
                if (setRefNodeId.find(nodeParentId) != setRefNodeId.cend()) {
                    const TDF_Label& refLabel = asmTree.nodeData(nodeParentId);
                    const QString refNodeName =
                            xdeDocItem->findLabelName(refLabel).trimmed();
                    if (!refNodeName.isEmpty() && refNodeName != guiNodeText) {
                        guiNodeText = QString::fromUtf8("%1 [\xe2\x86\x92%2]") // UTF8 rightwards arrow
                                      .arg(refNodeName, guiNodeText);
                    }
                    guiNodeId = nodeParentId;
                }
                guiNode->setText(0, guiNodeText);
                Internal::setTreeItemXdeAssemblyNode(
                            guiNode, XdeAssemblyNode(xdeDocItem, guiNodeId));
                const QIcon icon = Internal::xdeShapeIcon(xdeDocItem, nodeLabel);
                if (!icon.isNull())
                    guiNode->setIcon(0, icon);
                mapNodeIdToTreeItem.insert({ nodeId, guiNode });
            }
        }
        else {
            auto guiNode = Internal::guiCreateXdeTreeNode(guiParentNode, nodeId, xdeDocItem);
            mapNodeIdToTreeItem.insert({ nodeId, guiNode });
        }
    });
}

QTreeWidgetItem *WidgetApplicationTree::findTreeItemDocument(const Document *doc) const
{
    for (int i = 0; i < m_ui->treeWidget_App->topLevelItemCount(); ++i) {
        QTreeWidgetItem* treeItem = m_ui->treeWidget_App->topLevelItem(i);
        if (Internal::treeItemDocument(treeItem) == doc)
            return treeItem;
    }
    return nullptr;
}

QTreeWidgetItem *WidgetApplicationTree::findTreeItemDocumentItem(const DocumentItem *docItem) const
{
    QTreeWidgetItem* treeItemDoc = this->findTreeItemDocument(docItem->document());
    if (treeItemDoc != nullptr) {
        for (QTreeWidgetItemIterator it(treeItemDoc); *it; ++it) {
            if (Internal::treeItemDocumentItem(*it) == docItem)
                return *it;
        }
    }
    return nullptr;
}

void WidgetApplicationTree::onDocumentItemAdded(DocumentItem *docItem)
{
    QTreeWidgetItem* treeDocItem = this->loadDocumentItem(docItem);
    if (sameType<XdeDocumentItem>(docItem)) {
        auto xdeDocItem = static_cast<XdeDocumentItem*>(docItem);
        this->guiBuildXdeTree(treeDocItem, xdeDocItem);
    }
    QTreeWidgetItem* treeItemDoc = this->findTreeItemDocument(docItem->document());
    if (treeItemDoc != nullptr) {
        treeItemDoc->addChild(treeDocItem);
        treeItemDoc->setExpanded(true);
    }
}

void WidgetApplicationTree::onDocumentItemPropertyChanged(
        const DocumentItem *docItem, const Property *prop)
{
    QTreeWidgetItem* treeItemDocItem = this->findTreeItemDocumentItem(docItem);
    if (treeItemDocItem != nullptr) {
        if (prop == &docItem->propertyLabel)
            treeItemDocItem->setText(0, Internal::documentItemLabel(docItem));
    }
}

void WidgetApplicationTree::onTreeWidgetDocumentSelectionChanged(
        const QItemSelection &selected, const QItemSelection &deselected)
{
    const QModelIndexList listSelectedIndex = selected.indexes();
    const QModelIndexList listDeselectedIndex = deselected.indexes();
    std::vector<ApplicationItem> vecSelected;
    std::vector<ApplicationItem> vecDeselected;
    vecSelected.reserve(listSelectedIndex.size());
    vecDeselected.reserve(listDeselectedIndex.size());
    for (const QModelIndex& index : listSelectedIndex) {
        const QTreeWidgetItem* treeItem = m_ui->treeWidget_App->itemFromIndex(index);
        vecSelected.push_back(std::move(Internal::toApplicationItem(treeItem)));
    }
    for (const QModelIndex& index : listDeselectedIndex) {
        const QTreeWidgetItem* treeItem = m_ui->treeWidget_App->itemFromIndex(index);
        vecDeselected.push_back(std::move(Internal::toApplicationItem(treeItem)));
    }
    GuiApplication::instance()->selectionModel()->add(vecSelected);
    GuiApplication::instance()->selectionModel()->remove(vecDeselected);

    //emit selectionChanged();
}

} // namespace Mayo
