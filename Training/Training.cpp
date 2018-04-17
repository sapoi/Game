#include "Training.h"

namespace { // training private
    bool inProgress = false;
    const int IMAGE_SIDE_SIZE = 32;
    const float LINE_DRAWING_STEP_SIZE = 0.5;
    const int BATCH_COUNT = 20;
    const int BATCH_SIZE = 30000;
    const int EPOCH_COUNT_PER_BATCH = 6;
    Randomizer randomizer{};
    vector<unique_ptr<ShapeDescriptor>> shapeDescriptors{};
}

ShapeIndex Training::RegisterShapeDescriptor(unique_ptr<ShapeDescriptor> shapeDescriptor) {
    if (inProgress) {
        cout << "ERROR: cannot register new shape descriptor, training in progress" << endl;
        return UNKNOWN_SHAPE;
    }

    shapeDescriptors.push_back(move(shapeDescriptor));
    return ShapeIndex((int) shapeDescriptors.size() - 1);
}

ShapeDescriptor *RandomShapeDescriptor() {
    return shapeDescriptors[randomizer.next(shapeDescriptors.size())].get();
}

ImageLines
OffsetPoints(ShapeDescriptor *base, float offsetSize, float start, float momentum, float end, bool invalid) {
    float t = 0;
    float last_t = 0;
    vector<Line> lines;
    float2 lastBasePoint = base->GetPoint(t);
    float2 lastPoint = lastBasePoint;

    float2 nextBasePoint;
    float offset = 0;
    float target = 0;
    float step = 0;
    float2 shapeNormal;
    float2 nextPoint;
    while (t < 0.99) {
        t += 0.005;
        if (target == 0 && offset == 0 && t > start && t < end) {
            target = offsetSize;
            if (randomizer.yesOrNo()) {
                target *= -1;
            }
            step = (target - offset) * momentum;
        }
        offset += step;
        if (abs(offset) > abs(target)) {
            if (!invalid) {
                step *= -1;
            } else {
                target = randomizer.ratio() * offsetSize * 2 - offsetSize;
                step = (target - offset) * momentum;
            }
        }

        if (base->GetPoint(last_t, t, nextBasePoint)) {
            shapeNormal = {lastBasePoint.y - nextBasePoint.y, nextBasePoint.x - lastBasePoint.x};
            if (length(shapeNormal) > 0) {
                shapeNormal = normalize(shapeNormal);
                nextPoint = MovePoint(nextBasePoint, offset * shapeNormal);
            } else {
                nextPoint = nextBasePoint;
            }
            lines.emplace_back(lastPoint, nextPoint);
        } else {
            nextPoint = nextBasePoint;
        }
        lastBasePoint = nextBasePoint;
        lastPoint = nextPoint;
        last_t = t;
    }

    return ImageLines(lines);
};

ImageLines DrawShape(ShapeDescriptor *shapeDescriptor) {
    float t = 0;
    float last_t = 0;
    vector<Line> lines;
    float2 lastPoint = shapeDescriptor->GetPoint(t);
    float2 nextPoint;
    while (t < 0.99) {
        t += 0.005;
        if (shapeDescriptor->GetPoint(last_t, t, nextPoint)) {
            lines.emplace_back(lastPoint, nextPoint);
        }
        last_t = t;
        lastPoint = nextPoint;
    }

    return ImageLines(lines);
};

ImageLines ComposeShapes(ShapeDescriptor *base, ShapeDescriptor *modifier, int multiplication) {
    float t = 0;
    ImageLines image;
    while (t < 0.99) {
        float2 point = base->GetPoint(t);
        auto part = DrawShape(modifier);
        auto translation = CreateTranslationMatrix(point - float2{0.5, 0.5});
        auto scale = CreateScalingMatrix(float2(1.5f / multiplication, 1.5f / multiplication));
        part.Transform(mul<float, 3, 3>(translation, scale));

        image.Add(part);
        t += 1.f / multiplication / 1.5f;
    }

    return image;
};

void generateInvalidData(ofstream &trainingData,
                         int invalidDataCount, bool generate = false) {
    for (int i = 0; i < invalidDataCount; ++i) {
        if (i % 500 == 0) {
            cout << "generating: " << to_string(i) << endl;
        }

        ImageLines image;
        if (randomizer.ratio() > 0.7f) {
            float offset = randomizer.ratio() + 0.5f;
            if (randomizer.yesOrNo()) {
                offset *= -1.f;
            }
            image = OffsetPoints(RandomShapeDescriptor(), offset, randomizer.ratio() * 0.3f,
                                 randomizer.ratio() * 0.1f + 0.05f, 1, true);
        } else if (randomizer.ratio() > 0.1f) {
            vector<Line> lines;
            float2 start{randomizer.ratio(), randomizer.ratio()};
            float2 end;
            float2 previousStart;
            for (int k = 0; k < randomizer.nextExponential(); ++k) {
                float2 dir = start - previousStart;
                end = float2{randomizer.ratio(), randomizer.ratio()};
                if (randomizer.ratio() > 0.3) {
                    lines.emplace_back(start, (end + dir) * 0.3f);
                    previousStart = start;
                    start = end + dir;
                } else {
                    lines.emplace_back(start, end);
                    previousStart = start;
                    start = end;
                }
            }
            image.Add(ImageLines(lines));
        }

        auto rotation = CreateRotationMatrix(randomizer.ratio());
        image.Transform(rotation);
        image.Normalize();
        auto finalImage = DrawLines(image, IMAGE_SIDE_SIZE, LINE_DRAWING_STEP_SIZE);
        RandomizeData(finalImage, randomizer);

        if (generate) {
            std::string imageName = "generated/invalid" + std::to_string(i);
            finalImage.SaveToFile(imageName);
        }

        for (float grayScale : finalImage.Serialize()) {
            trainingData << grayScale << " ";
        }
        trainingData << std::endl;
        //output shape
        for (int j = 0; j < shapeDescriptors.size(); ++j) {
            trainingData << 0;
            trainingData << " ";
        }

        trainingData << std::endl;
    }
};

void Training::GenerateData(const std::string &filename, int validDataCount,
                            int invalidDataCount, bool generateImages) {
    if (shapeDescriptors.empty()) {
        cout << "ERROR: no shape descriptors registered" << endl;
    }

    ofstream trainingData;
    trainingData.open("batches/" + filename);
    int active = 0;

    trainingData << validDataCount + invalidDataCount << " " << IMAGE_SIDE_SIZE * IMAGE_SIDE_SIZE << " "
                 << shapeDescriptors.size() << std::endl;


    for (int i = 0; i < validDataCount; i++) {
        if (i % 500 == 0) {
            cout << "generating: " << to_string(i) << endl;
        }
        active = active % (int) shapeDescriptors.size();
        auto *activeDescriptor = shapeDescriptors[active].get();
        auto *activeModifier = RandomShapeDescriptor();

        ImageLines image;
        if (randomizer.ratio() > 0.5f)
            image = ComposeShapes(activeDescriptor, activeModifier, randomizer.next(8, 14));
        else {
            float offset = (randomizer.ratio() * 0.15f - 0.075f);
            image = OffsetPoints(activeDescriptor, offset, randomizer.ratio() * 0.7f + 0.05f,
                                 randomizer.ratio() * 0.1f,
                                 0.85f, false);
        }
        image.Normalize();

        auto poi = activeDescriptor->GetPointsOfInterest();
        if (!poi.empty()) {
            for (float3 p : poi) {
                if (randomizer.ratio() > 0.5) {
                    auto *innerShapeDescriptor = RandomShapeDescriptor();

                    ImageLines poiImage;
                    if (randomizer.ratio() > 0.7) {
                        poiImage = DrawShape(innerShapeDescriptor);
                    } else {
                        poiImage = ComposeShapes(innerShapeDescriptor, RandomShapeDescriptor(), randomizer.next(8, 14));
                    }
                    poiImage.Normalize();
                    float3 position = p;
                    auto translation = CreateTranslationMatrix(float2{position.x, position.y});
                    auto scaling = CreateScalingMatrix({position.z * 0.8f, position.z * 0.8f});
                    poiImage.Transform(mul<float, 3, 3>(translation, scaling));
                    image.Add(poiImage);
                }
            }
        }

        auto deformation = CreateScalingMatrix(
                float2{0.8f + randomizer.ratio() * 0.4f, 0.8f + randomizer.ratio() * 0.4f});
        image.Transform(deformation);

        image.Normalize();

        auto finalImage = DrawLines(image, IMAGE_SIDE_SIZE, LINE_DRAWING_STEP_SIZE);
        RandomizeData(finalImage, randomizer);
        if (generateImages) {
            std::string imageName = "generated/" + activeDescriptor->GetName() + std::to_string(i);
            finalImage.SaveToFile(imageName);
        }

        for (float grayScale : finalImage.Serialize()) {
            trainingData << grayScale << " ";
        }
        trainingData << std::endl;
        //output shape
        for (int j = 0; j < shapeDescriptors.size(); ++j) {
            trainingData << (j == active ? 1 : 0);
            trainingData << " ";
        }
        trainingData << std::endl;
        active += 1;
    }

    generateInvalidData(trainingData, invalidDataCount, generateImages);
    trainingData.close();
}


void Training::Train(string networkFile) {
    if (shapeDescriptors.empty()) {
        cout << "ERROR: no shape descriptors registered for training" << endl;
        return;
    }

    inProgress = true;
    TrainingCase network(networkFile,
                         vector<unsigned int>{IMAGE_SIDE_SIZE * IMAGE_SIDE_SIZE,
                                              (unsigned int)(shapeDescriptors.size()*60),
                                              (unsigned int)(shapeDescriptors.size()*8),
                                              (int) shapeDescriptors.size()});

//    for (int i = 0; i < BATCH_COUNT; ++i) {
//        cout << "generating batch " << i + 1 << " from " << BATCH_COUNT << endl;
//        GenerateData("batch" + to_string(i) + ".data", BATCH_SIZE * 2 / 3.0f, BATCH_SIZE / 3.0f, false);
//    }

    network.SetLearningParams(0.1, 0);

    network.TrainOnAllBatches(EPOCH_COUNT_PER_BATCH);
    network.TrainOnAllBatches(EPOCH_COUNT_PER_BATCH);
    network.TrainOnAllBatches(EPOCH_COUNT_PER_BATCH);

    network.Save(networkFile);
    inProgress = false;
}

void Training::ManualTraining(string networkFile) {
    using namespace Training;
    TrainingCase network(networkFile,
                         vector<unsigned int>{IMAGE_SIDE_SIZE * IMAGE_SIDE_SIZE, 512, 64,
                                              (int) shapeDescriptors.size()});
    string dataName = "data.data";
    string prefix;
    string networkName = "network.net";

    int validDataCount;
    int invalidDataCount;
    int epochCount;
    int repetitions;
    float learningRate;
    float learningMomentum;

    while (true) {
        char command;
        cout << "awaiting command" << endl;
        cin >> command;
        switch (command) {
            case 'g':
                cout << "valid:" << endl;
                cin >> validDataCount;
                cout << " invalid:" << endl;
                cin >> invalidDataCount;
                cout << "dataName:" << endl;
                cin >> dataName;

                GenerateData(dataName, validDataCount, invalidDataCount, false);
                cout << "data generated" << endl;
                continue;
            case 'l':
                cout << "load  data, dataName:" << endl;
                cin >> dataName;
                network.LoadData(dataName);
                cout << "data loaded" << endl;
                continue;
            case 'e':
                cout << "epochCount:" << endl;
                cin >> epochCount;
                network.Train(epochCount);
                continue;
            case 's':
                network.Save(networkName);
                cout << "saved" << endl;
                continue;
            case 't': {
                cout << "test, testFileName: " << endl;
                string testFileName;
                cin >> testFileName;
                cout << "RESULT: " << network.Test(testFileName) << endl;
            }
                continue;
            case 'm':
                cout << "learning rate (default 0.7): " << endl;
                cin >> learningRate;
                cout << "learning momentum (default 0): " << endl;
                cin >> learningMomentum;
                network.SetLearningParams(learningRate, learningMomentum);
                continue;
            case 'q':
                network.Save(networkName);
                return;
            case 'c':
                cout << "testFilesPrefix: " << endl;
                cin >> prefix;
                cout << "valid:" << endl;
                cin >> validDataCount;
                cout << " invalid:" << endl;
                cin >> invalidDataCount;
                cout << "epochCount:" << endl;
                cin >> epochCount;
                cout << "repetitions:" << endl;
                cin >> repetitions;
                for (int i = 0; i < repetitions; ++i) {
                    auto t = std::time(nullptr);
                    cout << "time: " << std::asctime(std::localtime(&t)) << endl;
                    cout << "repetition: " << i << " from " << repetitions << endl;
                    dataName = prefix + to_string(i) + ".data";
                    GenerateData(dataName, validDataCount, invalidDataCount, false);
                    network.LoadData(dataName);
                    cout << "data generated" << endl;
                    network.Train(epochCount);
                }
                continue;
            case 'b':
                cout << "epochCount: " << endl;
                cin >> epochCount;
                network.TrainOnAllBatches(epochCount);
                continue;
            case 'Q':
                return;
            default:
                cout << "invalid command" << endl;
                cout << "g generate" << endl;
                cout << "c complex" << endl;
                cout << "m learning params" << endl;
                cout << "e train" << endl;
                cout << "t test" << endl;
                cout << "s save" << endl;
                cout << "q quit" << endl;
                cout << "Q quit without save" << endl;
                continue;

        }
    }
};