#pragma once
#include "Editor.h"
#include <QQuickItem>
#include <map>
namespace music {
    class ScoreView:public QQuickItem {
        Q_OBJECT
        Q_PROPERTY(music::Editor* editor READ editor WRITE setEditor NOTIFY editorChanged)
        Q_PROPERTY(qreal viewportY READ viewportY WRITE setViewportY NOTIFY viewportChanged)
        Q_PROPERTY(qreal viewportHeight READ viewportHeight WRITE setViewportHeight NOTIFY viewportChanged)
        public:
        explicit ScoreView(QQuickItem* parent=nullptr);
        [[nodiscard]] Editor* editor() const {
            return editor_;
        }
        void setEditor(Editor* editor);
        [[nodiscard]] qreal viewportY() const {
            return viewportY_;
        }
        [[nodiscard]] qreal viewportHeight() const {
            return viewportHeight_;
        }
        void setViewportY(qreal y);
        void setViewportHeight(qreal height);
        [[nodiscard]] int cachedSystemCount() const {
            return cache_.size();
        }
        signals:
        void editorChanged();
        void viewportChanged();
        protected:
        QSGNode* updatePaintNode(QSGNode*,UpdatePaintNodeData*) override;
        void mousePressEvent(QMouseEvent*) override;
        void mouseDoubleClickEvent(QMouseEvent*) override;
        private:
        struct CachedSystem {
            SystemSnapshot snapshot;
            QImage image;
            int revision=0;
        };
        void updateVisibleCache();
        void updateSelectionLayer();
        void resetCache();
        void invalidateFrom(int firstSystem);
        void click(QMouseEvent* event,bool doubleClick);
        Editor* editor_=nullptr;
        qreal viewportY_=0,viewportHeight_=600;
        int nextRevision_=1;
        std::map<int,CachedSystem> cache_;
        QImage selectionImage_;
        QRectF selectionBounds_;
        int selectionRevision_=0;
        EventId renderedSelection_=0;
        std::size_t renderedTone_=0;
        int renderedSystemRevision_=-1;
        std::vector<QRectF> rangeRects_;
        int rangeRevision_=0;
        bool rangeDirty_=true;
        bool resetNodes_=true;
    };
}
// namespace music
