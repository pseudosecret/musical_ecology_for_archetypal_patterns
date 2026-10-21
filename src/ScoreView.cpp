#include "ScoreView.h"
#include <QMouseEvent>
#include <QQuickWindow>
#include <QSGSimpleTextureNode>
#include <QSGSimpleRectNode>
#include <algorithm>
#include <cmath>
#include <memory>
namespace music {
    namespace {
        class SystemNode:public QSGSimpleTextureNode {
            public:int index=-1,revision=0;
        };
        class SelectionNode:public QSGSimpleTextureNode {
            public:int revision=-1;
        };
        class RangeLayer:public QSGNode {
            public:int revision=-1;
        };
    }
    ScoreView::ScoreView(QQuickItem* parent):QQuickItem(parent) {
        setFlag(ItemHasContents);
        setAcceptedMouseButtons(Qt::LeftButton);
    }
    void ScoreView::setEditor(Editor* editor) {
        if(editor_==editor)return;
        if(editor_)disconnect(editor_,nullptr,this,nullptr);
        editor_=editor;
        if(editor_) {
            connect(editor_,&QObject::destroyed,this,[this] { editor_=nullptr; resetCache(); emit editorChanged(); });
            connect(editor_,&Editor::layoutInvalidated,this,&ScoreView::invalidateFrom);
            connect(editor_,&Editor::scoreReset,this,&ScoreView::resetCache);
            connect(editor_,&Editor::selectionChanged,this,[this] { rangeDirty_=true; updateSelectionLayer(); });
        }
        resetCache();
        emit editorChanged();
    }
    void ScoreView::setViewportY(qreal y) {
        if(qFuzzyCompare(viewportY_,y))return;
        viewportY_=y;
        updateVisibleCache();
        update();
        emit viewportChanged();
    }
    void ScoreView::setViewportHeight(qreal height) {
        if(qFuzzyCompare(viewportHeight_,height))return;
        viewportHeight_=height;
        updateVisibleCache();
        update();
        emit viewportChanged();
    }
    void ScoreView::resetCache() {
        resetNodes_=true;
        rangeDirty_=true;
        cache_.clear();
        updateVisibleCache();
        update();
    }
    void ScoreView::invalidateFrom(int first) {
        rangeDirty_=true;
        cache_.erase(cache_.lower_bound(first),cache_.end());
        updateVisibleCache();
        update();
    }
    void ScoreView::updateVisibleCache() {
        if(!editor_) {
            updateSelectionLayer();
            return;
        }
        const auto& layout=editor_->layoutIndex();
        const int first=std::max(0,static_cast<int>(std::floor(viewportY_/layout.systemHeight()))-1),last=std::min(editor_->systemCount(),static_cast<int>(std::ceil((viewportY_+viewportHeight_)/layout.systemHeight()))+1);
        for(auto it=cache_.begin();it!=cache_.end();)if(it->first<first || it->first>=last) { it=cache_.erase(it); rangeDirty_=true; }
        else ++it;
        for(int i=first;i<last;++i)if(!cache_.contains(i)) {
            rangeDirty_=true;
            auto snapshot=Engraving::layout(editor_->score(),layout,i);
            auto image=Engraving::render(snapshot);
            cache_.emplace(i,CachedSystem {
                std::move(snapshot),std::move(image),nextRevision_++
            });
        }
        updateSelectionLayer();
    }
    void ScoreView::updateSelectionLayer() {
        if(rangeDirty_) {
            std::vector<QRectF> rects;
            if(editor_)for(const auto& [i,cached]:cache_) {
                const auto systemRects=editor_->rangeRects(i);
                rects.insert(rects.end(),systemRects.begin(),systemRects.end());
            }
            rangeDirty_=false;
            if(rects!=rangeRects_) {
                rangeRects_=std::move(rects);
                ++rangeRevision_;
                update();
            }
        }
        const auto id=editor_?editor_->selectedEventId():0;
        const auto tone=editor_?editor_->selectedTone():0;
        const auto system=editor_?static_cast<int>(editor_->layoutIndex().systemForMeasure(editor_->selectedMeasure())):0;
        const auto it=cache_.find(system);
        const auto revision=it==cache_.end()?-1:it->second.revision;
        if(renderedSelection_==id && renderedTone_==tone && renderedSystemRevision_==revision)return;
        renderedSelection_=id;
        renderedTone_=tone;
        renderedSystemRevision_=revision;
        selectionImage_={};
        selectionBounds_={};
        ++selectionRevision_;
        if(editor_ && id) {
            const auto& layout=editor_->layoutIndex();
            if(it!=cache_.end()) {
                selectionImage_=Engraving::renderSelection(it->second.snapshot,id,tone,selectionBounds_);
                selectionBounds_.translate(0,system*layout.systemHeight());
            }
        }
        update();
    }
    QSGNode* ScoreView::updatePaintNode(QSGNode* old,UpdatePaintNodeData*) {
        if(resetNodes_) {
            // Reattach replacement textures through the item's transform/clip tree.
            std::unique_ptr<QSGNode> previous(old);
            old=nullptr;
            resetNodes_=false;
        }
        auto* root=old;
        if(!root) {
            root=std::make_unique<QSGNode>().release();
            root->appendChildNode(std::make_unique<QSGNode>().release());
            root->appendChildNode(std::make_unique<RangeLayer>().release());
            root->appendChildNode(std::make_unique<QSGNode>().release());
        }
        auto* systems=root->firstChild();
        for(auto* child=systems->firstChild();child;) {
            auto* next=child->nextSibling();
            auto* node=static_cast<SystemNode*>(child);
            const auto cached=cache_.find(node->index);
            if(cached==cache_.end() || cached->second.revision!=node->revision) {
                systems->removeChildNode(child);
                std::unique_ptr<QSGNode> removed(child);
            }
            child=next;
        }
        if(!window())return root;
        for(const auto& [i,c]:cache_) {
            bool exists=false;
            for(auto* child=systems->firstChild();child;child=child->nextSibling())if(static_cast<SystemNode*>(child)->index==i) {
                exists=true;
                break;
            }
            if(exists)continue;
            auto node=std::make_unique<SystemNode>();
            node->index=i;
            node->revision=c.revision;
            node->setTexture(window()->createTextureFromImage(c.image));
            node->setOwnsTexture(true);
            node->setRect(QRectF(0,i*editor_->layoutIndex().systemHeight(),c.snapshot.width,c.snapshot.height));
            systems->appendChildNode(node.release());
        }
        auto* rangeLayer=static_cast<RangeLayer*>(root->firstChild()->nextSibling());
        if(rangeLayer->revision!=rangeRevision_) {
            while(auto* child=rangeLayer->firstChild()) {
                rangeLayer->removeChildNode(child);
                std::unique_ptr<QSGNode> removed(child);
            }
            for(const auto& rect:rangeRects_)rangeLayer->appendChildNode(std::make_unique<QSGSimpleRectNode>(rect,QColor("#282c70d5")).release());
            rangeLayer->revision=rangeRevision_;
        }
        auto* selectionLayer=root->lastChild();
        auto* selection=static_cast<SelectionNode*>(selectionLayer->firstChild());
        if(selection && selection->revision!=selectionRevision_) {
            selectionLayer->removeChildNode(selection);
            std::unique_ptr<QSGNode> removed(selection);
            selection=nullptr;
        }
        if(!selection && !selectionImage_.isNull()) {
            auto node=std::make_unique<SelectionNode>();
            node->setTexture(window()->createTextureFromImage(selectionImage_));
            node->setOwnsTexture(true);
            node->setRect(selectionBounds_);
            node->revision=selectionRevision_;
            selectionLayer->appendChildNode(node.release());
        }
        return root;
    }
    void ScoreView::click(QMouseEvent* event,bool twice) {
        if(!editor_)return;
        const qreal h=editor_->layoutIndex().systemHeight();
        const int index=std::floor(event->position().y()/h);
        const auto it=cache_.find(index);
        if(it==cache_.end())return;
        const QPointF point(event->position().x(),event->position().y()-index*h);
        if(const auto hit=Engraving::hit(it->second.snapshot,point)) {
            forceActiveFocus();
            editor_->activateHit(*hit,twice,event->modifiers());
            event->accept();
        }
    }
    void ScoreView::mousePressEvent(QMouseEvent* event) {
        click(event,false);
    }
    void ScoreView::mouseDoubleClickEvent(QMouseEvent* event) {
        click(event,true);
    }
}
// namespace music
