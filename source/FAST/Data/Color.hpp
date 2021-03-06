#pragma once

#include "FAST/Data/DataTypes.hpp"
#include <cctype> // std::tolower

namespace fast {

class FAST_EXPORT  Color {
    private:
        Vector3f mColorVector;
    public:
        Color() : mColorVector(Vector3f(0, 0, 0)) {};

        Color(float red, float green, float blue) : mColorVector(Vector3f(red, green, blue)) {};

        static Color fromString(std::string str) {
            std::transform(str.begin(), str.end(), str.begin(),
                [](unsigned char c) { return std::tolower(c); });
            std::map<std::string, Color> mapping = {
                {"black", Color::Black()},
                {"white", Color::White()},
                {"red", Color::Red()},
                {"blue", Color::Blue()},
                {"green", Color::Green()},
                {"yellow", Color::Yellow()},
                {"cyan", Color::Cyan()},
                {"magenta", Color::Magenta()},
            };

            if(mapping.count(str) == 0)
                throw Exception("Could not find color " + str);

            return mapping[str];
        }

        Vector3f asVector() const {
            return mColorVector;
        }
        float getRedValue() const {
            return mColorVector.x();
        }
        float getGreenValue() const {
            return mColorVector.y();
        }
        float getBlueValue() const {
            return mColorVector.z();
        }
        static Color Red() {
            return Color(1,0,0);
        }
        static Color Green() {
            return Color(0,1,0);
        }
        static Color Blue() {
            return Color(0,0,1);
        }
        static Color White() {
            return Color(1,1,1);
        }
        static Color Black() {
            return Color(0,0,0);
        }
        static Color Yellow() {
            return Color(1, 1, 0);
        }
        static Color Magenta() {
            return Color(1, 0, 1);
        }
        static Color Cyan() {
            return Color(0, 1, 1);
        }

};


}

