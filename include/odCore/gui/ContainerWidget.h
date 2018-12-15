/*
 * ContainerWidget.h
 *
 *  Created on: 15 Jul 2018
 *      Author: zal
 */

#ifndef INCLUDE_GUI_CONTAINERWIDGET_H_
#define INCLUDE_GUI_CONTAINERWIDGET_H_

#include <vector>

#include <odCore/gui/Widget.h>

namespace odGui
{

    class ContainerWidget : public Widget
    {
    public:

        ContainerWidget();

        void addWidget(Widget *w);
        void removeWidget(Widget *w);

        virtual void flattenDrawables(const glm::mat4 &parentMatrix) override;


    protected:

        virtual void intersectChildren(const glm::vec2 &pointNdc, const glm::mat4 &parentMatrix, const glm::mat4 &parentInverseMatrix, std::vector<HitWidgetInfo> &hitWidgets) override;


    private:

        std::vector<od::RefPtr<Widget>> mChildWidgets;

    };

}

#endif /* INCLUDE_GUI_CONTAINERWIDGET_H_ */
