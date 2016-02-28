#include "gpa.h"

GPA::GPA(Matrix<std::complex<double>> img)
{
    // initialise vectors
    _Phases.resize(2);
    _Image = std::make_shared<Matrix<std::complex<double>>>(img);
    _FFT = std::make_shared<Matrix<std::complex<double>>>(Matrix<std::complex<double>>(_Image->rows(), _Image->cols()));

    // create fftw plans
    _FFTplan = std::make_shared<fftw_plan>(fftw_plan_dft_2d(_Image->rows(), _Image->cols(), NULL, NULL, FFTW_FORWARD, FFTW_ESTIMATE));
    _IFFTplan = std::make_shared<fftw_plan>(fftw_plan_dft_2d(_Image->rows(), _Image->cols(), NULL, NULL, FFTW_BACKWARD, FFTW_ESTIMATE));

    // do the FFT now
    UtilsFFT::doFFTPlan(_FFTplan, UtilsFFT::preFFTShift(*_Image), *_FFT);
}

std::shared_ptr<Matrix<std::complex<double>>> GPA::getImage()
{
    return _Image;
}

std::shared_ptr<Matrix<std::complex<double>>> GPA::getFFT()
{
    return _FFT;
}

std::shared_ptr<Matrix<double>> GPA::getExx()
{
    return _Exx;
}

std::shared_ptr<Matrix<double>> GPA::getExy()
{
    return _Exy;
}

std::shared_ptr<Matrix<double>> GPA::getEyx()
{
    return _Eyx;
}

std::shared_ptr<Matrix<double>> GPA::getEyy()
{
    return _Eyy;
}

int GPA::getGVectors()
{
    int xs = _FFT->cols();
    int ys = _FFT->rows();

    //Matrix<double> _PS(ys, xs);

    Matrix<std::complex<double>> _PS(ys, xs);
    Matrix<std::complex<double>> hanned = UtilsMaths::HannWindow(*_Image);
    UtilsFFT::doFFTPlan(_FFTplan, UtilsFFT::preFFTShift(hanned), _PS);
            //= UtilsMaths::HannWindow(original_image);;

    // get power spectrum as this will be used quite a bit
    for (int xIndex = 0; xIndex < xs; ++xIndex)
      for (int yIndex = 0; yIndex < ys; ++yIndex)
          _PS(yIndex, xIndex) =  std::log10(1+std::abs( _PS(yIndex, xIndex) ));

    // convert back to matrix coordinates
    int x0 = xs/2;
    int y0 = ys/2;

    double max = std::real(_PS(y0, x0));

    std::vector<double> averages;
    std::vector<double> vals;

    // loop through radii and sum
    for (int r = 1; r < std::min(xs, ys) / 4 - 1; ++r)
    {
        vals.clear();

        //get values within band
        for (int j = 0; j <= y0+r; ++j)
            for (int i = 0; i <= x0+r; ++i)
            {
                double dist = UtilsMaths::Distance(x0, y0, i, j);
                if (dist < r+1 && dist > r-1)
                    vals.push_back(std::real(_PS(j, i)));
            }

        std::sort(vals.begin(), vals.end(), std::greater<double>());

        double av = 0;
        int sz = std::min((int)vals.size(), 10);

        for (int i = 0; i < sz; ++i)
            av += vals[i];
        av /= (sz * max);

        averages.push_back(av);
    }

    // normalise
    auto minIterator = std::min_element(averages.begin(), averages.end());
    double min = *minIterator;
    for (int i = 0; i < averages.size(); ++i)
        averages[i] -= min;
    auto maxIterator = std::max_element(averages.begin(), averages.end());
    max = *maxIterator;
    for (int i = 0; i < averages.size(); ++i)
        averages[i] /= max;

    int inner = 0;

    for(int i = 0; i < averages.size(); ++i)
    {
        if (averages[i] < 0.9 * max)
        {
            inner = i;
            break;
        }
        averages[i] *= 0;
    }

    for(int i = 0; i < averages.size(); ++i)
    {
        averages[i] *= (averages.size() - i);
    }

    // just finds the maximum value not in the centre.
    int radius = std::distance(averages.begin(), std::max_element(averages.begin()+inner, averages.end()));

    return radius;
}


void GPA::calculatePhase(int i, double gx, double gy, double sig)
{
    //if (i != 0 && i != 1)
        //throw

    _Phases[i] = std::make_shared<Phase>(Phase(_FFT, gx, gy, sig, _FFTplan, _IFFTplan));
}

std::shared_ptr<Phase> GPA::getPhase(int i)
{
    //if (i != 0 && i != 1)
        //throw

    return _Phases[i];
}

Matrix<double> GPA::_GetRotationMatrix(double angle)
{
    Matrix<double> rotmat(2, 2);
    angle*= 180/PI;

    rotmat(0, 0) = std::cos(angle);
    rotmat(0, 1) = std::sin(angle);
    rotmat(1, 0) = -1*std::sin(angle);
    rotmat(1, 1) = std::cos(angle);

    return rotmat;
}

void GPA::calculateStrain(double angle)
{
    Matrix<std::complex<double>> d1dx, d1dy, d2dx, d2dy;

    _Phases[0]->getDifferential(d1dx, d1dy);
    _Phases[1]->getDifferential(d2dx, d2dy);

    //calculate A matrix (from G matrix)
    //here I do several steps in one go, but all I am doing is Inverse(Transpose(G)) = A
    Matrix<double> A(2,2);
    Coord2D<double> g1 = _Phases[0]->getGVector();
    Coord2D<double> g2 = _Phases[1]->getGVector();

    double factor = 1.0 / (g1.x * g2.y - g1.y * g2.x);

    // invert the transpose
    A(0, 0) = factor * g2.y;
    A(1, 0) = factor * -g2.x;
    A(0, 1) = factor * -g1.y;
    A(1, 1) = factor * g1.x;

    A = Inner(A, _GetRotationMatrix(angle));

    // reusing factor
    factor = -1.0 / (2.0 * PI);

    _Exx = std::make_shared<Matrix<double>>( jml::Real(factor * (A(0, 0) * d1dx + A(0, 1) * d2dx)));
    _Exy = std::make_shared<Matrix<double>>( jml::Real(factor * (A(0, 0) * d1dy + A(0, 1) * d2dy)));
    _Eyx = std::make_shared<Matrix<double>>( jml::Real(factor * (A(1, 0) * d1dx + A(1, 1) * d2dx)));
    _Eyy = std::make_shared<Matrix<double>>( jml::Real(factor * (A(1, 0) * d1dy + A(1, 1) * d2dy)));
}