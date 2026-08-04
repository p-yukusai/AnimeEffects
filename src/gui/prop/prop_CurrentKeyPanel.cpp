#include "cmnd/ScopedMacro.h"
#include "cmnd/BasicCommands.h"
#include "core/Constant.h"
#include "core/TimeKeyExpans.h"
#include "core/DepthKey.h"
#include "core/BlurKey.h"
#include "ctrl/TimeLineUtil.h"
#include "gui/ResourceDialog.h"
#include "gui/prop/prop_CurrentKeyPanel.h"
#include "gui/prop/prop_Items.h"

namespace gui {
namespace prop {

    //-------------------------------------------------------------------------------------------------
    MoveKeyGroup::MoveKeyGroup(Panel& aPanel, KeyAccessor& aAccessor, int aLabelWidth, GUIResources* mGUIResources):
        KeyGroup(tr("Move"), aLabelWidth),
        mAccessor(aAccessor),
        mKnocker(),
        mEasing(),
        mSpline(),
        mPosition(),
        mCentroid(),
        mKeyExists(false) {
        mKnocker = new KeyKnocker(tr("Move"));
        mKnocker->set([=]() {
            this->mAccessor.knockNewMove();
            this->makeSureExpand();
        });
        aPanel.addGroup(mKnocker);

        {
            aPanel.addGroup(this);

            // easing
            mEasing = new EasingItem(this, mGUIResources);
            this->addItem(tr("Easing :"), mEasing);
            mEasing->onValueUpdated = [=](util::Easing::Param, util::Easing::Param aNext) {
                this->mAccessor.assignMoveEasing(aNext);
            };

            // spline
            mSpline = new ComboItem(this);
            mSpline->box().addItems(
                QStringList() << "Linear"
                              << "CatmullRom"
            );
            mSpline->setValue(core::MoveKey::kDefaultSplineType, false);
            mSpline->onValueUpdated = [=](int, int aNext) { this->mAccessor.assignMoveSpline(aNext); };
            this->addItem(tr("Spline :"), mSpline);

            // position
            mPosition = new Vector2DItem(this);
            mPosition->setRange(core::Constant::transMin(), core::Constant::transMax());
            mPosition->onValueUpdated = [=](QVector2D, QVector2D aNext) { this->mAccessor.assignMovePosition(aNext); };
            this->addItem(tr("Position :"), mPosition);

            // centroid
            mCentroid = new Vector2DItem(this);
            mCentroid->setRange(core::Constant::transMin(), core::Constant::transMax());
            mCentroid->onValueUpdated = [=](QVector2D, QVector2D aNext) { this->mAccessor.assignMoveCentroid(aNext); };
            this->addItem(tr("Centroid :"), mCentroid);
        }
        setKeyEnabled(false);
        setKeyExists(false);
    }

    void MoveKeyGroup::setKeyEnabled(bool aEnabled) {
        mKnocker->setEnabled(aEnabled);
        this->setEnabled(aEnabled);
    }

    void MoveKeyGroup::setKeyExists(bool aIsExists) {
        mKeyExists = aIsExists;
        mKnocker->setVisible(!aIsExists);
        this->setVisible(aIsExists);
    }

    void MoveKeyGroup::setKeyValue(const core::TimeKey* aKey) {
        TIMEKEY_PTR_TYPE_ASSERT(aKey, Move);
        const core::MoveKey::Data data = ((const core::MoveKey*)aKey)->data();
        mEasing->setValue(data.easing(), false);
        mSpline->setValue(data.spline(), false);
        mPosition->setValue(data.pos());
        mCentroid->setValue(data.centroid());
    }

    bool MoveKeyGroup::keyExists() const { return mKeyExists; }

    //-------------------------------------------------------------------------------------------------
    RotateKeyGroup::RotateKeyGroup(Panel& aPanel, KeyAccessor& aAccessor, int aLabelWidth, GUIResources* mGUIResources):
        KeyGroup(tr("Rotate"), aLabelWidth), mAccessor(aAccessor), mKnocker(), mEasing(), mRotate(), mKeyExists(false) {
        using util::MathUtil;
        mKnocker = new KeyKnocker(tr("Rotate"));
        mKnocker->set([=]() {
            this->mAccessor.knockNewRotate();
            this->makeSureExpand();
        });
        aPanel.addGroup(mKnocker);

        {
            aPanel.addGroup(this);

            // easing
            mEasing = new EasingItem(this, mGUIResources);
            this->addItem(tr("Easing :"), mEasing);
            mEasing->onValueUpdated = [=](util::Easing::Param, util::Easing::Param aNext) {
                this->mAccessor.assignRotateEasing(aNext);
            };

            // rotate
            mRotate = new DecimalItem(this);
            mRotate->setRange(
                MathUtil::getDegreeFromRadian(core::Constant::rotateMin()),
                MathUtil::getDegreeFromRadian(core::Constant::rotateMax())
            );
            mRotate->onValueUpdated = [=](double, double aNext) {
                this->mAccessor.assignRotateAngle(MathUtil::getRadianFromDegree(aNext));
            };
            this->addItem(tr("Angle :"), mRotate);
        }
        setKeyEnabled(false);
        setKeyExists(false);
    }

    void RotateKeyGroup::setKeyEnabled(bool aEnabled) {
        mKnocker->setEnabled(aEnabled);
        this->setEnabled(aEnabled);
    }

    void RotateKeyGroup::setKeyExists(bool aIsExists) {
        mKeyExists = aIsExists;
        mKnocker->setVisible(!aIsExists);
        this->setVisible(aIsExists);
    }

    void RotateKeyGroup::setKeyValue(const core::TimeKey* aKey) {
        TIMEKEY_PTR_TYPE_ASSERT(aKey, Rotate);
        const core::RotateKey::Data data = ((const core::RotateKey*)aKey)->data();
        mEasing->setValue(data.easing(), false);
        mRotate->setValue(util::MathUtil::getDegreeFromRadian(data.rotate()));
    }

    bool RotateKeyGroup::keyExists() const { return mKeyExists; }

    //-------------------------------------------------------------------------------------------------
    ScaleKeyGroup::ScaleKeyGroup(Panel& aPanel, KeyAccessor& aAccessor, int aLabelWidth, GUIResources* mGUIResources):
        KeyGroup(tr("Scale"), aLabelWidth), mAccessor(aAccessor), mKnocker(), mEasing(), mScale(), mKeyExists(false) {
        mKnocker = new KeyKnocker(tr("Scale"));
        mKnocker->set([=]() {
            this->mAccessor.knockNewScale();
            this->makeSureExpand();
        });
        aPanel.addGroup(mKnocker);

        {
            aPanel.addGroup(this);

            // easing
            mEasing = new EasingItem(this, mGUIResources);
            this->addItem(tr("Easing :"), mEasing);
            mEasing->onValueUpdated = [=](util::Easing::Param, util::Easing::Param aNext) {
                this->mAccessor.assignScaleEasing(aNext);
            };

            // scale
            mScale = new Vector2DItem(this);
            mScale->setRange(core::Constant::scaleMin(), core::Constant::scaleMax());
            mScale->onValueUpdated = [=](QVector2D, QVector2D aNext) { this->mAccessor.assignScaleRate(aNext); };
            this->addItem(tr("Rate :"), mScale);
        }
        setKeyEnabled(false);
        setKeyExists(false);
    }

    void ScaleKeyGroup::setKeyEnabled(bool aEnabled) {
        mKnocker->setEnabled(aEnabled);
        this->setEnabled(aEnabled);
    }

    void ScaleKeyGroup::setKeyExists(bool aIsExists) {
        mKeyExists = aIsExists;
        mKnocker->setVisible(!aIsExists);
        this->setVisible(aIsExists);
    }

    void ScaleKeyGroup::setKeyValue(const core::TimeKey* aKey) {
        TIMEKEY_PTR_TYPE_ASSERT(aKey, Scale);
        const core::ScaleKey::Data data = ((const core::ScaleKey*)aKey)->data();
        mEasing->setValue(data.easing(), false);
        mScale->setValue(data.scale());
    }

    bool ScaleKeyGroup::keyExists() const { return mKeyExists; }

    //-------------------------------------------------------------------------------------------------
    DepthKeyGroup::DepthKeyGroup(Panel& aPanel, KeyAccessor& aAccessor, int aLabelWidth, GUIResources* mGUIResources):
        KeyGroup(tr("Depth"), aLabelWidth), mAccessor(aAccessor), mKnocker(), mEasing(), mDepth(), mKeyExists(false) {
        mKnocker = new KeyKnocker(tr("Depth"));
        mKnocker->set([=]() {
            this->mAccessor.knockNewDepth();
            this->makeSureExpand();
        });
        aPanel.addGroup(mKnocker);

        {
            aPanel.addGroup(this);

            // easing
            mEasing = new EasingItem(this, mGUIResources);
            this->addItem(tr("Easing :"), mEasing);
            mEasing->onValueUpdated = [=](util::Easing::Param, util::Easing::Param aNext) {
                this->mAccessor.assignDepthEasing(aNext);
            };

            // depth
            mDepth = new DecimalItem(this);
            mDepth->setRange(core::Constant::transMin(), core::Constant::transMax());
            mDepth->onValueUpdated = [=](double, double aNext) { this->mAccessor.assignDepthPosition(aNext); };
            this->addItem(tr("Position :"), mDepth);
        }
        setKeyEnabled(false);
        setKeyExists(false);
    }

    void DepthKeyGroup::setKeyEnabled(bool aEnabled) {
        mKnocker->setEnabled(aEnabled);
        this->setEnabled(aEnabled);
    }

    void DepthKeyGroup::setKeyExists(bool aIsExists) {
        mKeyExists = aIsExists;
        mKnocker->setVisible(!aIsExists);
        this->setVisible(aIsExists);
    }

    void DepthKeyGroup::setKeyValue(const core::TimeKey* aKey) {
        TIMEKEY_PTR_TYPE_ASSERT(aKey, Depth);
        const core::DepthKey::Data& data = ((const core::DepthKey*)aKey)->data();
        mEasing->setValue(data.easing(), false);
        mDepth->setValue(data.depth());
    }

    bool DepthKeyGroup::keyExists() const { return mKeyExists; }

    //-------------------------------------------------------------------------------------------------
    OpaKeyGroup::OpaKeyGroup(Panel& aPanel, KeyAccessor& aAccessor, int aLabelWidth, GUIResources* mGUIResources):
        KeyGroup(tr("Opacity"), aLabelWidth),
        mAccessor(aAccessor),
        mKnocker(),
        mEasing(),
        mOpacity(),
        mKeyExists(false) {
        mKnocker = new KeyKnocker(tr("Opacity"));
        mKnocker->set([=]() {
            this->mAccessor.knockNewOpacity();
            this->makeSureExpand();
        });
        aPanel.addGroup(mKnocker);

        {
            aPanel.addGroup(this);

            // easing
            mEasing = new EasingItem(this, mGUIResources);
            this->addItem(tr("Easing :"), mEasing);
            mEasing->onValueUpdated = [=](util::Easing::Param, util::Easing::Param aNext) {
                this->mAccessor.assignOpaEasing(aNext);
            };

            // opacity
            mOpacity = new DecimalItem(this);
            mOpacity->setRange(0.0f, 1.0f);
            mOpacity->box().setSingleStep(0.1);
            mOpacity->onValueUpdated = [=](double, double aNext) { this->mAccessor.assignOpacity(aNext); };
            this->addItem(tr("Rate :"), mOpacity);
        }
        setKeyEnabled(false);
        setKeyExists(false);
    }

    void OpaKeyGroup::setKeyEnabled(bool aEnabled) {
        mKnocker->setEnabled(aEnabled);
        this->setEnabled(aEnabled);
    }

    void OpaKeyGroup::setKeyExists(bool aIsExists) {
        mKeyExists = aIsExists;
        mKnocker->setVisible(!aIsExists);
        this->setVisible(aIsExists);
    }

    void OpaKeyGroup::setKeyValue(const core::TimeKey* aKey) {
        TIMEKEY_PTR_TYPE_ASSERT(aKey, Opa);
        const core::OpaKey::Data& data = ((const core::OpaKey*)aKey)->data();
        mEasing->setValue(data.easing(), false);
        mOpacity->setValue(data.opacity());
    }

    bool OpaKeyGroup::keyExists() const { return mKeyExists; }

    //-------------------------------------------------------------------------------------------------
    BlurKeyGroup::BlurKeyGroup(Panel& aPanel, KeyAccessor& aAccessor, int aLabelWidth, GUIResources* mGUIResources):
        KeyGroup(tr("Blur"), aLabelWidth),
        mAccessor(aAccessor),
        mKnocker(),
        mEasing(),
        mAmount(),
        mBlurX(),
        mBlurY(),
        mAngle(),
        mDirectional(),
        mKeyExists(false) {
        mKnocker = new KeyKnocker(tr("Blur"));
        mKnocker->set([=]() {
            this->mAccessor.knockNewBlur();
            this->makeSureExpand();
        });
        aPanel.addGroup(mKnocker);

        {
            aPanel.addGroup(this);

            // easing
            mEasing = new EasingItem(this, mGUIResources);
            this->addItem(tr("Easing :"), mEasing);
            mEasing->onValueUpdated = [=](util::Easing::Param, util::Easing::Param aNext) {
                this->mAccessor.assignBlurEasing(aNext);
            };

            // amount (isotropic radius, the default/direct mode)
            mAmount = new DecimalItem(this);
            mAmount->setRange(0.0, 512.0);
            mAmount->box().setSingleStep(0.5);
            mAmount->onValueUpdated = [=](double, double aNext) { this->mAccessor.assignBlur((float)aNext); };

            // directional: extra content-space axes + angle unlocked by a checkbox
            mDirectional = new CheckItem(this);
            mDirectional->onValueUpdated = [=](bool aNext) {
                this->mAccessor.assignBlurDirectional(aNext);
                this->updateDirectionalRows(aNext);
            };

            // blur X/Y radii and angle in the layer's content space; each edit keeps the
            // other axes from the current key data
            mBlurX = new DecimalItem(this);
            mBlurX->setRange(0.0, 512.0);
            mBlurX->box().setSingleStep(0.5);
            mBlurX->onValueUpdated = [=](double, double) { this->assignBlurAxis(mBlurX); };

            mBlurY = new DecimalItem(this);
            mBlurY->setRange(0.0, 512.0);
            mBlurY->box().setSingleStep(0.5);
            mBlurY->onValueUpdated = [=](double, double) { this->assignBlurAxis(mBlurY); };

            mAngle = new DecimalItem(this);
            mAngle->setRange(-180.0, 180.0);
            mAngle->box().setSingleStep(1.0);
            mAngle->onValueUpdated = [=](double, double) { this->assignBlurAxis(mAngle); };

            this->addItem(tr("Amount :"), mAmount);
            this->addItem(tr("Directional: "), mDirectional);
            this->addItem(tr("Blur X :"), mBlurX);
            this->addItem(tr("Blur Y :"), mBlurY);
            this->addItem(tr("Angle :"), mAngle);
        }
        setKeyEnabled(false);
        setKeyExists(false);
        updateDirectionalRows(false);
    }

    void BlurKeyGroup::setKeyEnabled(bool aEnabled) {
        mKnocker->setEnabled(aEnabled);
        this->setEnabled(aEnabled);
    }

    void BlurKeyGroup::setKeyExists(bool aIsExists) {
        mKeyExists = aIsExists;
        mKnocker->setVisible(!aIsExists);
        this->setVisible(aIsExists);
    }

    void BlurKeyGroup::setKeyValue(const core::TimeKey* aKey) {
        TIMEKEY_PTR_TYPE_ASSERT(aKey, Blur);
        const core::BlurKey::Data& data = ((const core::BlurKey*)aKey)->data();
        mEasing->setValue(data.easing(), false);
        mAmount->setValue(data.amount());
        // setValue (not box().setChecked) so the item's change stamp stays in sync -
        // a stale stamp swallows the user's next click (mStamp == value() == no signal)
        mDirectional->setValue(data.isDirectional(), false);
        mBlurX->setValue(data.blurX());
        mBlurY->setValue(data.blurY());
        mAngle->setValue(data.angleDeg());
        updateDirectionalRows(data.isDirectional());
    }

    bool BlurKeyGroup::keyExists() const { return mKeyExists; }

    void BlurKeyGroup::updateDirectionalRows(bool aVisible) {
        this->setItemVisible(mBlurX, aVisible);
        this->setItemVisible(mBlurY, aVisible);
        this->setItemVisible(mAngle, aVisible);
    }

    void BlurKeyGroup::assignBlurAxis(DecimalItem* aEdited) {
        const core::BlurKey::Data cur = mAccessor.blurData();
        float blurX = cur.blurX();
        float blurY = cur.blurY();
        float angle = cur.angleDeg();
        if (aEdited == mBlurX) {
            blurX = (float)mBlurX->value();
        } else if (aEdited == mBlurY) {
            blurY = (float)mBlurY->value();
        } else {
            angle = (float)mAngle->value();
        }
        mAccessor.assignBlurEllipse(blurX, blurY, angle);
    }

    //-------------------------------------------------------------------------------------------------
    HSVKeyGroup::HSVKeyGroup(Panel& aPanel, KeyAccessor& aAccessor, int aLabelWidth, GUIResources* mGUIResources):
        KeyGroup(tr("HSV"), aLabelWidth),
        mAccessor(aAccessor),
        mKnocker(),
        mEasing(),
        mHue(),
        mSaturation(),
        mValue(),
        mKeyExists(false) {
        mKnocker = new KeyKnocker(tr("HSV"));
        mKnocker->set([=]() {
            this->mAccessor.knockNewHSV();
            this->makeSureExpand();
        });
        aPanel.addGroup(mKnocker);

        {
            aPanel.addGroup(this);

            // easing
            mEasing = new EasingItem(this, mGUIResources);
            this->addItem(tr("Easing :"), mEasing);
            mEasing->onValueUpdated = [=](util::Easing::Param, util::Easing::Param aNext) {
                this->mAccessor.assignHSVEasing(aNext);
            };

            // hue
            mHue = new IntegerItem(this);
            mHue->setRange(0, 360);
            mHue->box().setSingleStep(1);
            mHue->onValueUpdated = [=](int, int aNext) { this->mAccessor.assignHSV(aNext, "hue"); };

            // saturation
            mSaturation = new IntegerItem(this);
            mSaturation->setRange(0, 200);
            mSaturation->box().setSingleStep(1);
            mSaturation->onValueUpdated = [=](int, int aNext) { this->mAccessor.assignHSV(aNext, "sat"); };

            // value
            mValue = new IntegerItem(this);
            mValue->setRange(0, 200);
            mValue->box().setSingleStep(1);
            mValue->onValueUpdated = [=](int, int aNext) { this->mAccessor.assignHSV(aNext, "val"); };

            // absolute
            mAbsolute = new CheckItem(this);
            mAbsolute->onValueUpdated = [=](bool aNext) { this->mAccessor.assignHSV(int(aNext), "abs"); };

            this->addItem(tr("Hue :"), mHue);
            this->addItem(tr("Saturation :"), mSaturation);
            this->addItem(tr("Value : "), mValue);
            this->addItem(tr("Absolute color: "), mAbsolute);
        }
        setKeyEnabled(false);
        setKeyExists(false);
    }

    void HSVKeyGroup::setKeyEnabled(bool aEnabled) {
        mKnocker->setEnabled(aEnabled);
        this->setEnabled(aEnabled);
    }

    void HSVKeyGroup::setKeyExists(bool aIsExists) {
        mKeyExists = aIsExists;
        mKnocker->setVisible(!aIsExists);
        this->setVisible(aIsExists);
    }

    void HSVKeyGroup::setKeyValue(const core::TimeKey* aKey) {
        TIMEKEY_PTR_TYPE_ASSERT(aKey, HSV);
        const core::HSVKey::Data& data = ((const core::HSVKey*)aKey)->data();
        mEasing->setValue(data.easing(), false);
        mHue->setValue(data.hsv()[0]);
        mSaturation->setValue(data.hsv()[1]);
        mValue->setValue(data.hsv()[2]);
        mAbsolute->setValue(bool(data.hsv()[3]), false);
    }

    bool HSVKeyGroup::keyExists() const { return mKeyExists; }

    //-------------------------------------------------------------------------------------------------
    PoseKeyGroup::PoseKeyGroup(Panel& aPanel, KeyAccessor& aAccessor, int aLabelWidth, GUIResources* mGUIResources):
        KeyGroup(tr("Pose"), aLabelWidth), mAccessor(aAccessor), mKnocker(), mEasing(), mKeyExists(false) {
        mKnocker = new KeyKnocker(tr("Pose"));
        mKnocker->set([=]() {
            this->mAccessor.knockNewPose();
            this->makeSureExpand();
        });
        aPanel.addGroup(mKnocker);

        {
            aPanel.addGroup(this);

            // easing
            mEasing = new EasingItem(this, mGUIResources);
            this->addItem(tr("Easing :"), mEasing);
            mEasing->onValueUpdated = [=](util::Easing::Param, util::Easing::Param aNext) {
                this->mAccessor.assignPoseEasing(aNext);
            };
        }
        setKeyEnabled(false);
        setKeyExists(false, false);
    }

    void PoseKeyGroup::setKeyEnabled(bool aEnabled) {
        mKnocker->setEnabled(aEnabled);
        this->setEnabled(aEnabled);
    }

    void PoseKeyGroup::setKeyExists(bool aIsExists, bool aIsKnockable) {
        mKeyExists = aIsExists;
        mKnocker->setVisible(!aIsExists && aIsKnockable);
        this->setVisible(aIsExists);
    }

    void PoseKeyGroup::setKeyValue(const core::TimeKey* aKey) {
        TIMEKEY_PTR_TYPE_ASSERT(aKey, Pose);
        const core::PoseKey::Data& data = ((const core::PoseKey*)aKey)->data();
        mEasing->setValue(data.easing(), false);
    }

    bool PoseKeyGroup::keyExists() const { return mKeyExists; }

    //-------------------------------------------------------------------------------------------------
    FFDKeyGroup::FFDKeyGroup(Panel& aPanel, KeyAccessor& aAccessor, int aLabelWidth, GUIResources* mGUIResources):
        KeyGroup(tr("FFD"), aLabelWidth), mAccessor(aAccessor), mKnocker(), mEasing(), mKeyExists(false) {
        mKnocker = new KeyKnocker(tr("FFD"));
        mKnocker->set([=]() {
            this->mAccessor.knockNewFFD();
            this->makeSureExpand();
        });
        aPanel.addGroup(mKnocker);

        {
            aPanel.addGroup(this);

            // easing
            mEasing = new EasingItem(this, mGUIResources);
            this->addItem(tr("Easing :"), mEasing);
            mEasing->onValueUpdated = [=](util::Easing::Param, util::Easing::Param aNext) {
                this->mAccessor.assignFFDEasing(aNext);
            };
        }
        setKeyEnabled(false);
        setKeyExists(false, false);
    }

    void FFDKeyGroup::setKeyEnabled(bool aEnabled) {
        mKnocker->setEnabled(aEnabled);
        this->setEnabled(aEnabled);
    }

    void FFDKeyGroup::setKeyExists(bool aIsExists, bool aIsKnockable) {
        mKeyExists = aIsExists;
        mKnocker->setVisible(!aIsExists && aIsKnockable);
        this->setVisible(aIsExists);
    }

    void FFDKeyGroup::setKeyValue(const core::TimeKey* aKey) {
        TIMEKEY_PTR_TYPE_ASSERT(aKey, FFD);
        const core::FFDKey::Data& data = ((const core::FFDKey*)aKey)->data();
        mEasing->setValue(data.easing(), false);
    }

    bool FFDKeyGroup::keyExists() const { return mKeyExists; }

    //-------------------------------------------------------------------------------------------------
    ImageKeyGroup::ImageKeyGroup(Panel& aPanel, KeyAccessor& aAccessor, int aLabelWidth, ViaPoint& aViaPoint, GUIResources* mGUIResources):
        KeyGroup(tr("Image"), aLabelWidth),
        mAccessor(aAccessor),
        mKnocker(),
        mBrowse(),
        mOffset(),
        mCellSize(),
        mKeyExists(false),
        mViaPoint(aViaPoint) {
        mKnocker = new KeyKnocker(tr("Image"));
        mKnocker->set([=]() {
            this->knockNewKey();
            this->makeSureExpand();
        });
        aPanel.addGroup(mKnocker);

        {
            aPanel.addGroup(this);

            // browse
            mBrowse = new BrowseItem(this);
            mBrowse->onButtonPushed = [=]() {
                auto resNode = this->mViaPoint.requireOneResource();
                if (resNode) {
                    this->mBrowse->setValue(resNode->data().identifier());
                    this->mAccessor.assignImageResource(*resNode);
                }
            };
            this->addItem(tr("Resource :"), mBrowse);

            // offset
            mOffset = new Vector2DItem(this);
            mOffset->setRange(core::Constant::transMin(), core::Constant::transMax());
            mOffset->onValueUpdated = [=](QVector2D, QVector2D aNext) { this->mAccessor.assignImageOffset(-aNext); };
            this->addItem(tr("Center :"), mOffset);

            // cell size
            mCellSize = new IntegerItem(this);
            mCellSize->setRange(core::Constant::imageCellSizeMin(), core::Constant::imageCellSizeMax());
            mCellSize->onValueUpdated = [=](int, int aNext) { this->mAccessor.assignImageCellSize(aNext); };
            this->addItem(tr("Cell size :"), mCellSize);
        }
        setKeyEnabled(false);
        setKeyExists(false, false);
    }

    void ImageKeyGroup::knockNewKey() {
        auto resNode = mViaPoint.requireOneResource();
        if (resNode) {
            this->mAccessor.knockNewImage(resNode->handle());
        }
    }

    void ImageKeyGroup::setKeyEnabled(bool aEnabled) {
        mKnocker->setEnabled(aEnabled);
        this->setEnabled(aEnabled);
    }

    void ImageKeyGroup::setKeyExists(bool aIsExists, bool aIsKnockable) {
        mKeyExists = aIsExists;
        mKnocker->setVisible(!aIsExists && aIsKnockable);
        this->setVisible(aIsExists);
    }

    void ImageKeyGroup::setKeyValue(const core::TimeKey* aKey) {
        TIMEKEY_PTR_TYPE_ASSERT(aKey, Image);
        const core::ImageKey::Data& data = ((const core::ImageKey*)aKey)->data();
        mBrowse->setValue(data.resource()->identifier());
        mOffset->setValue(-data.imageOffset());
        mCellSize->setValue(data.gridMesh().cellSize());
    }

    bool ImageKeyGroup::keyExists() const { return mKeyExists; }

    //-------------------------------------------------------------------------------------------------
    CurrentKeyPanel::CurrentKeyPanel(
        ViaPoint& aViaPoint, core::Project& aProject, const QString& aTitle, QWidget* aParent, GUIResources* mGUIResources
    ):
        Panel(aTitle, aParent),
        mViaPoint(aViaPoint),
        mGUIResources(mGUIResources),
        mProject(aProject),
        mTarget(),
        mKeyAccessor(),
        mLabelWidth(),
        mMovePanel(),
        mRotatePanel(),
        mScalePanel(),
        mDepthPanel(),
        mOpaPanel(),
        mHSVPanel(),
        mBlurPanel(),
        mPosePanel(),
        mFFDPanel(),
        mImagePanel() {
        mKeyAccessor.setProject(&aProject);
        mLabelWidth = this->fontMetrics().boundingRect(tr("Max text width :")).width();

        build();
        this->hide();
    }

    void CurrentKeyPanel::setTarget(core::ObjectNode* aTarget) {
        mTarget = aTarget;
        mKeyAccessor.setTarget(aTarget);

        if (mTarget) {
            this->setTitle(mTarget->name() + " : " + tr("Current Keys"));
            this->show();
        } else {
            this->hide();
        }

        updateKey();
    }

    void CurrentKeyPanel::setPlayBackActivity(bool aIsActive) {
        // resume
        if (!this->isEnabled() && !aIsActive) {
            updateKeyExists();
            updateKeyValue();
        }
        this->setEnabled(!aIsActive);
    }

    void CurrentKeyPanel::updateKey() {
        updateKeyExists();
        updateKeyValue();
    }

    void CurrentKeyPanel::updateFrame() {
        if (this->isEnabled()) {
            updateKeyExists();
            updateKeyValue();
        }
    }

    void CurrentKeyPanel::build() {
        using core::Constant;

        mMovePanel.reset(new MoveKeyGroup(*this, mKeyAccessor, mLabelWidth, mGUIResources));
        mRotatePanel.reset(new RotateKeyGroup(*this, mKeyAccessor, mLabelWidth, mGUIResources));
        mScalePanel.reset(new ScaleKeyGroup(*this, mKeyAccessor, mLabelWidth, mGUIResources));
        mDepthPanel.reset(new DepthKeyGroup(*this, mKeyAccessor, mLabelWidth, mGUIResources));
        mOpaPanel.reset(new OpaKeyGroup(*this, mKeyAccessor, mLabelWidth, mGUIResources));
        mHSVPanel.reset(new HSVKeyGroup(*this, mKeyAccessor, mLabelWidth, mGUIResources));
        mBlurPanel.reset(new BlurKeyGroup(*this, mKeyAccessor, mLabelWidth, mGUIResources));
        mPosePanel.reset(new PoseKeyGroup(*this, mKeyAccessor, mLabelWidth, mGUIResources));
        mFFDPanel.reset(new FFDKeyGroup(*this, mKeyAccessor, mLabelWidth, mGUIResources));
        mImagePanel.reset(new ImageKeyGroup(*this, mKeyAccessor, mLabelWidth, mViaPoint, mGUIResources));
        this->adjustSize();
        this->addStretch();
    }

    void CurrentKeyPanel::updateKeyExists() {
        const bool enabled = mTarget && mTarget->timeLine();

        mMovePanel->setKeyEnabled(enabled);
        mRotatePanel->setKeyEnabled(enabled);
        mScalePanel->setKeyEnabled(enabled);
        mDepthPanel->setKeyEnabled(enabled);
        mOpaPanel->setKeyEnabled(enabled);
        mHSVPanel->setKeyEnabled(enabled);
        mBlurPanel->setKeyEnabled(enabled);
        mPosePanel->setKeyEnabled(enabled);
        mFFDPanel->setKeyEnabled(enabled);
        mImagePanel->setKeyEnabled(enabled);

        if (enabled) {
            const core::TimeLine& timeLine = *mTarget->timeLine();
            const int frame = mProject.animator().currentFrame().get();
            const bool hasAreaBone = timeLine.current().bone().areaKey();
            const bool hasAnyMesh = mTarget->hasAnyMesh();
            const bool hasAnyImage = mTarget->hasAnyImage();

            mMovePanel->setKeyExists(timeLine.hasTimeKey(core::TimeKeyType_Move, frame));
            mRotatePanel->setKeyExists(timeLine.hasTimeKey(core::TimeKeyType_Rotate, frame));
            mScalePanel->setKeyExists(timeLine.hasTimeKey(core::TimeKeyType_Scale, frame));
            mDepthPanel->setKeyExists(timeLine.hasTimeKey(core::TimeKeyType_Depth, frame));
            mOpaPanel->setKeyExists(timeLine.hasTimeKey(core::TimeKeyType_Opa, frame));
            mHSVPanel->setKeyExists(timeLine.hasTimeKey(core::TimeKeyType_HSV, frame));
            mBlurPanel->setKeyExists(timeLine.hasTimeKey(core::TimeKeyType_Blur, frame));
            mPosePanel->setKeyExists(timeLine.hasTimeKey(core::TimeKeyType_Pose, frame), hasAreaBone);
            mFFDPanel->setKeyExists(timeLine.hasTimeKey(core::TimeKeyType_FFD, frame), hasAnyMesh);
            mImagePanel->setKeyExists(timeLine.hasTimeKey(core::TimeKeyType_Image, frame), hasAnyImage);
        }
    }

    void CurrentKeyPanel::updateKeyValue() {
        if (mTarget && mTarget->timeLine()) {
            auto& timeLine = *mTarget->timeLine();
            const int frame = mProject.animator().currentFrame().get();

            if (mMovePanel->keyExists()) {
                mMovePanel->setKeyValue(timeLine.timeKey(core::TimeKeyType_Move, frame));
            }
            if (mRotatePanel->keyExists()) {
                mRotatePanel->setKeyValue(timeLine.timeKey(core::TimeKeyType_Rotate, frame));
            }
            if (mScalePanel->keyExists()) {
                mScalePanel->setKeyValue(timeLine.timeKey(core::TimeKeyType_Scale, frame));
            }
            if (mDepthPanel->keyExists()) {
                mDepthPanel->setKeyValue(timeLine.timeKey(core::TimeKeyType_Depth, frame));
            }
            if (mOpaPanel->keyExists()) {
                mOpaPanel->setKeyValue(timeLine.timeKey(core::TimeKeyType_Opa, frame));
            }
            if (mHSVPanel->keyExists()) {
                mHSVPanel->setKeyValue(timeLine.timeKey(core::TimeKeyType_HSV, frame));
            }
            if (mBlurPanel->keyExists()) {
                mBlurPanel->setKeyValue(timeLine.timeKey(core::TimeKeyType_Blur, frame));
            }
            if (mPosePanel->keyExists()) {
                mPosePanel->setKeyValue(timeLine.timeKey(core::TimeKeyType_Pose, frame));
            }
            if (mFFDPanel->keyExists()) {
                mFFDPanel->setKeyValue(timeLine.timeKey(core::TimeKeyType_FFD, frame));
            }
            if (mImagePanel->keyExists()) {
                mImagePanel->setKeyValue(timeLine.timeKey(core::TimeKeyType_Image, frame));
            }
        }
    }

} // namespace prop
} // namespace gui
