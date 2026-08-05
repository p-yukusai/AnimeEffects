#include "qjsonarray.h"
#include "qjsonobject.h"
#include "util/MathUtil.h"
#include "core/Bone2.h"

#include "BoneKey.h"
#include "ObjectNode.h"

namespace core {

Bone2::Bone2():
    TreeNodeBase(this),
    mOrigin(),
    mLocalPos(),
    mLocalAngle(),
    mRange(),
    mShape(),
    mBindingNodes(),
    mWorldPos(),
    mWorldAngle(),
    mRotate(),
    mFocus(),
    mSelect() {}

Bone2::Bone2(const Bone2& aRhs):
    TreeNodeBase(this),
    mOrigin(aRhs.mOrigin),
    mLocalPos(aRhs.mLocalPos),
    mLocalAngle(aRhs.mLocalAngle),
    mRange(aRhs.mRange),
    mShape(aRhs.mShape),
    mBindingNodes(aRhs.mBindingNodes),
    mWorldPos(aRhs.mWorldPos),
    mWorldAngle(aRhs.mWorldAngle),
    mRotate(aRhs.mRotate),
    mFocus(),
    mSelect() {}

Bone2::~Bone2() { qDeleteAll(children().begin(), children().end()); }

void Bone2::setWorldPos(const QVector2D& aWorldPos, const Bone2* aParent) {
    XC_ASSERT(!mOrigin);

    if (aParent) {
        const QVector2D dir = aWorldPos - aParent->worldPos();
        mLocalPos = QVector2D(dir.length(), 0.0f);
        mLocalAngle =
            util::MathUtil::getAngleDifferenceRad(aParent->worldAngle(), util::MathUtil::getAngleRad(dir)) - mRotate;
    } else {
        mLocalPos = aWorldPos;
        mLocalAngle = 0.0f - mRotate;
    }
}

void Bone2::setShape(const BoneShape& aShape) {
    XC_ASSERT(!mOrigin);
    mShape = aShape;
}

const BoneShape& Bone2::shape() const { return mOrigin ? mOrigin->mShape : mShape; }

QList<ObjectNode*>& Bone2::bindingNodes() {
    XC_ASSERT(!mOrigin);
    return mBindingNodes;
}
const QList<ObjectNode*>& Bone2::bindingNodes() const {
    XC_ASSERT(!mOrigin);
    return mBindingNodes;
}

bool Bone2::isBinding(const ObjectNode& aNode) const {
    for (const auto node : mBindingNodes) {
        if (node == &aNode)
            return true;
    }
    return false;
}

void Bone2::setRotate(float aRotate) { mRotate = aRotate; }

float Bone2::rotate() const { return mRotate; }

const QVector2D& Bone2::localPos() const { return mOrigin ? mOrigin->mLocalPos : mLocalPos; }

float Bone2::localAngle() const { return mOrigin ? mOrigin->mLocalAngle : mLocalAngle; }

void Bone2::setRange(int aIndex, const QVector2D& aRange) {
    XC_ASSERT(!mOrigin);
    XC_ASSERT(0 <= aIndex && aIndex < 2);
    mRange.at(aIndex) = aRange;
}

const QVector2D& Bone2::range(int aIndex) const {
    XC_ASSERT(0 <= aIndex && aIndex < 2);
    return mOrigin ? mOrigin->mRange.at(aIndex) : mRange.at(aIndex);
}

bool Bone2::hasValidRange() const {
    return mOrigin ? mOrigin->hasValidRange() : (!mRange[0].isNull() || !mRange[1].isNull());
}

QVector2D Bone2::blendedRange(float aRate) const {
    return mOrigin ? mOrigin->blendedRange(aRate) : (mRange[0] * (1.0f - aRate) + mRange[1] * aRate);
}

void Bone2::updateWorldTransform() {
    if (parent()) {
        mWorldAngle = parent()->worldAngle() + localAngle() + mRotate;
        const QVector2D dir = util::MathUtil::getRotateVectorRad(localPos(), mWorldAngle);
        mWorldPos = parent()->worldPos() + dir;
    } else {
        mWorldPos = localPos();
        mWorldAngle = localAngle();
    }

    for (auto child : children()) {
        child->updateWorldTransform();
    }
}

const QVector2D& Bone2::worldPos() const { return mWorldPos; }

float Bone2::worldAngle() const { return mWorldAngle; }

QMatrix4x4 Bone2::transformationMatrix(const QVector2D& aToPos, float aToAngle) const {
    static constexpr QVector3D kRotateAxis(0.0f, 0.0f, 1.0f);
    const float rotate = util::MathUtil::getDegreeFromRadian(aToAngle - worldAngle());

    QMatrix4x4 mtx;
    mtx.translate(aToPos.x(), aToPos.y());
    mtx.rotate(rotate, kRotateAxis);
    mtx.translate(-worldPos().x(), -worldPos().y());
    return mtx;
}

QMatrix4x4 Bone2::transformationMatrix(const Bone2& aTo) const {
    return transformationMatrix(aTo.worldPos(), aTo.worldAngle());
}

QMatrix4x4 Bone2::transformationMatrix(const QMatrix4x4& aToMtx) const {
    QMatrix4x4 myInvMtx;
    {
        static constexpr QVector3D kRotateAxis(0.0f, 0.0f, 1.0f);
        myInvMtx.rotate(-util::MathUtil::getDegreeFromRadian(worldAngle()), kRotateAxis);
        myInvMtx.translate(-worldPos().x(), -worldPos().y());
    }

    return aToMtx * myInvMtx;
}

Bone2* Bone2::createShadow() const {
    if (mOrigin) {
        return mOrigin->createShadow();
    }

    Bone2* shadow = new Bone2(*this);

    if (shadow) {
        shadow->mOrigin = this;
        shadow->mFocus = util::LifeLink::Node();
        shadow->mSelect = util::LifeLink::Node();

        // recursive
        for (auto child : children()) {
            Bone2* childShadow = child->createShadow();
            if (childShadow) {
                shadow->children().pushBack(childShadow);
            }
        }
    }
    return shadow;
}

bool Bone2::serialize(Serializer& aOut) const {
    aOut.writeID(this);
    aOut.writeID(mOrigin);

    aOut.write(mLocalPos);
    aOut.write(mLocalAngle);
    aOut.write(mRange[0]);
    aOut.write(mRange[1]);

    if (!mShape.serialize(aOut)) {
        return false;
    }

    aOut.write(static_cast<int>(mBindingNodes.count()));
    for (const auto node : mBindingNodes) {
        aOut.writeID(node);
    }

    aOut.write(mWorldPos);
    aOut.write(mWorldAngle);
    aOut.write(mRotate);

    return aOut.checkStream();
}

template<typename vec2d>
static void addVecToJson(vec2d vector, QJsonObject* json, QString varName) {
    json->insert(varName + "X", vector.x());
    json->insert(varName + "Y", vector.y());
}

static QVector2D objToVec(QJsonObject obj, const QString& varName) {
    return {static_cast<float>(obj[varName + "X"].toDouble()), static_cast<float>(obj[varName + "Y"].toDouble())};
}

static ObjectNode* getChild(ObjectNode* aNode, const QString& aName) {
    for (const auto child : aNode->children()) {
        if (child->name() == aName) { return child; }
        if (!child->children().empty()) {
            if (const auto recChild = getChild(child, aName)) { return recChild; }
        }
    }
    return nullptr;
}

static ObjectNode* getNode(Project* aProject, const QString& aName) {
    auto curNode = aProject->objectTree().topNode();
    if (curNode->name() == aName) { return curNode; }
    while (curNode) {
        if (const auto recChild = getChild(curNode, aName)) { return recChild; }
        curNode = curNode->nextSib();
    }
    return nullptr;
}

void Bone2::deserializeFromJson(QJsonObject json, Project* aProject, const Bone2* origin) {
    // Maybe I'll handle this at some point but since by necessity the caches will be wiped i don't see the point
    json = json["Bone"].toObject();
   [[maybe_unused]]const sint32 originId = json["OriginID"].toInt();
    mOrigin = origin;
    mLocalPos = objToVec(json, "LocalPos");
    mLocalAngle = static_cast<float>(json["LocalAngle"].toDouble());
    mRange[0] = objToVec(json, "Range0");
    mRange[1] = objToVec(json, "Range1");
    mShape.deserializeFromJson(json);
    mWorldPos = objToVec(json, "WorldPos");
    mWorldAngle = static_cast<float>(json["WorldAngle"].toDouble());
    mRotate = static_cast<float>(json["Rotate"].toDouble());
    // TODO: Testing
    const int bindCount = json["Nodes"].toInt();
    if (bindCount < 0) {
        for (int i = 0; i < bindCount; ++i) {
            const QString nodeName = json["Nodes"].toArray()[i].toString();
            if (auto* node = getNode(aProject, nodeName)) {
                mBindingNodes.push_back(node);
            }
        }
    }
    const int childCount = json["ChildCount"].toInt();
    // iterate children
    for (int i = 0; i < childCount; ++i) {
        auto* child = new Bone2();
        this->children().pushBack(child);
        child->deserializeFromJson(json["Children"].toArray()[i].toObject(), aProject, nullptr);
    }
}

QJsonObject Bone2::serializeToJson() {
    QJsonObject bone;
    const auto id = boneIDAssigner.getId(this);
    bone["BoneID"] = id;
    bone["OriginID"] = boneIDAssigner.getId(mOrigin);
    addVecToJson(mLocalPos, &bone, "LocalPos");
    bone["LocalAngle"] = mLocalAngle;
    addVecToJson(mRange.at(0), &bone, "Range0");
    addVecToJson(mRange.at(1), &bone, "Range1");
    bone["Shape"] = mShape.serializeToJson();
    addVecToJson(mWorldPos, &bone, "WorldPos");
    bone["WorldAngle"] = mWorldAngle;
    bone["Rotate"] = mRotate;
    bone["NodeSize"] = mBindingNodes.size();
    QJsonArray nodes;
    for (const auto node : mBindingNodes) {
        nodes.append(node->name());
    }
    bone["Nodes"] = nodes;
    bone["ChildCount"] = static_cast<int>(children().size());
    QJsonArray boneArray;
    for (const auto child : children()) {
        QJsonObject childBone;
        childBone["Bone"] = child->serializeToJson();
        boneArray.append(childBone);
    }
    bone["Children"] = boneArray;
    return bone;
}

bool Bone2::deserialize(Deserializer& aIn) {
    if (!aIn.bindIDData(this)) {
        return aIn.errored("failed to bind reference id");
    }

    // order to write a pointer later
    {
        auto solver = [=](void* aPtr) { this->mOrigin = (Bone2*)aPtr; };
        if (!aIn.orderIDData(solver)) {
            return aIn.errored("invalid reference id");
        }
    }

    aIn.read(mLocalPos);
    aIn.read(mLocalAngle);
    aIn.read(mRange[0]);
    aIn.read(mRange[1]);

    if (!mShape.deserialize(aIn)) {
        return false;
    }

    {
        int bindCount = 0;
        aIn.read(bindCount);
        if (bindCount < 0)
            return false;

        for (int i = 0; i < bindCount; ++i) {
            auto solver = [=](void* aPtr) {
                if (aPtr) {
                    this->mBindingNodes.push_back((ObjectNode*)aPtr);
                }
            };
            if (!aIn.orderIDData(solver)) {
                return aIn.errored("invalid reference id");
            }

            if (aIn.failure()) {
                return aIn.errored("stream error");
            }
        }
    }

    aIn.read(mWorldPos);
    aIn.read(mWorldAngle);
    aIn.read(mRotate);

    return aIn.checkStream();
}

} // namespace core
