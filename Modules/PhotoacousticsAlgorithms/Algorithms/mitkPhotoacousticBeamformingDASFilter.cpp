/*===================================================================

The Medical Imaging Interaction Toolkit (MITK)

Copyright (c) German Cancer Research Center,
Division of Medical and Biological Informatics.
All rights reserved.

This software is distributed WITHOUT ANY WARRANTY; without
even the implied warranty of MERCHANTABILITY or FITNESS FOR
A PARTICULAR PURPOSE.

See LICENSE.txt or http://www.mitk.org for details.

===================================================================*/

#define _USE_MATH_DEFINES

#include "mitkPhotoacousticBeamformingDASFilter.h"
#include "mitkProperties.h"
#include "mitkImageReadAccessor.h"
#include <algorithm>
#include <itkImageIOBase.h>
#include <chrono>
#include <cmath>


mitk::BeamformingDASFilter::BeamformingDASFilter() : m_OutputData(nullptr), m_InputData(nullptr)
{
  this->SetNumberOfIndexedInputs(1);
  this->SetNumberOfRequiredInputs(1);

  m_Conf.Pitch = 0.0003;
  m_Conf.SpeedOfSound = 1540;
  m_Conf.SamplesPerLine = 2048;
  m_Conf.ReconstructionLines = 128;
  m_Conf.RecordTime = 0.00006;
  m_Conf.TransducerElements = 128;
}

mitk::BeamformingDASFilter::~BeamformingDASFilter()
{
}

void mitk::BeamformingDASFilter::GenerateInputRequestedRegion()
{
  Superclass::GenerateInputRequestedRegion();

  mitk::Image* output = this->GetOutput();
  mitk::Image* input = const_cast< mitk::Image * > (this->GetInput());
  if (!output->IsInitialized())
  {
    return;
  }

  input->SetRequestedRegionToLargestPossibleRegion();

  //GenerateTimeInInputRegion(output, input);
}

void mitk::BeamformingDASFilter::GenerateOutputInformation()
{
  mitk::Image::ConstPointer input = this->GetInput();
  mitk::Image::Pointer output = this->GetOutput();

  if ((output->IsInitialized()) && (this->GetMTime() <= m_TimeOfHeaderInitialization.GetMTime()))
    return;

  itkDebugMacro(<< "GenerateOutputInformation()");
  
  unsigned int dim[] = { m_Conf.ReconstructionLines, m_Conf.SamplesPerLine, input->GetDimension(2)};
  output->Initialize(mitk::MakeScalarPixelType<double>(), 3, dim);

  mitk::Vector3D spacing;
  spacing[0] = m_Conf.Pitch * m_Conf.TransducerElements * 1000 / m_Conf.ReconstructionLines;
  spacing[1] = m_Conf.RecordTime * m_Conf.SpeedOfSound / 2 * 1000 / m_Conf.SamplesPerLine;
  spacing[2] = 1;

  output->GetGeometry()->SetSpacing(spacing);
  output->GetGeometry()->Modified();
  output->SetPropertyList(input->GetPropertyList()->Clone());

  m_TimeOfHeaderInitialization.Modified();
}

void mitk::BeamformingDASFilter::GenerateData()
{
  mitk::Image::ConstPointer input = this->GetInput();
  mitk::Image::Pointer output = this->GetOutput();

  double inputS = input->GetDimension(1);
  double inputL = input->GetDimension(0);

  double outputS = output->GetDimension(1);
  double outputL = output->GetDimension(0);

  double* VonHannWindow = VonHannFunction(m_Conf.TransducerElements * 2);

  if (!output->IsInitialized())
  {
    return;
  }

  auto begin = std::chrono::high_resolution_clock::now(); // debbuging the performance...

  for (int i = 0; i < output->GetDimension(2); ++i) // seperate Slices should get Beamforming seperately applied
  {
    mitk::ImageReadAccessor inputReadAccessor(input, input->GetSliceData(i));

    m_OutputData = new double[m_Conf.ReconstructionLines*m_Conf.SamplesPerLine];
    m_InputDataPuffer = new double[input->GetDimension(0)*input->GetDimension(1)];

    if (input->GetPixelType().GetTypeAsString() == "scalar (double)")
    {
      m_InputData = (double*)inputReadAccessor.GetData();
    }
    else if (input->GetPixelType().GetTypeAsString() == "scalar (short)")
    {
      short* InputPuffer = (short*)inputReadAccessor.GetData();
      for (int l = 0; l < inputL; ++l)
      {
        for (int s = 0; s < inputS; ++s)
        {
          m_InputDataPuffer[l*(unsigned short)inputS + s] = (double)InputPuffer[l*(unsigned short)inputS + s];
        }
      }
      m_InputData = m_InputDataPuffer;
    }
    else if (input->GetPixelType().GetTypeAsString() == "scalar (float)")
    {
      float* InputPuffer = (float*)inputReadAccessor.GetData();
      for (int l = 0; l < inputL; ++l)
      {
        for (int s = 0; s < inputS; ++s)
        {
          m_InputDataPuffer[l*(unsigned short)inputS + s] = (double)InputPuffer[l*(unsigned short)inputS + s];
        }
      }
      m_InputData = m_InputDataPuffer;
    }
    else
    {
      MITK_INFO << "Could not determine pixel type";
      return;
    }

    for (int l = 0; l < outputL; ++l)
    {
      for (int s = 0; s < outputS; ++s)
      {
        m_OutputData[l*(unsigned short)outputS + s] = 0;
      }
    }

    unsigned short AddSample = 0;
    unsigned short maxLine = 0;
    unsigned short minLine = 0;
    double delayMultiplicator = 0;
    double l_i = 0;
    double s_i = 0;

    double part = 0.07 * inputL;
    double tan_phi = std::tan(m_Conf.Angle / 360 * 2 * M_PI);
    double part_multiplicator = tan_phi * m_Conf.RecordTime / inputS * m_Conf.SpeedOfSound / m_Conf.Pitch;

    double VH_mult = 1;

    double l = 0;
    double x = 0;
    double root = 0;

    if (m_Conf.DelayCalculationMethod == beamformingSettings::DelayCalc::Linear)
    {
      //linear delay
      for (unsigned short line = 0; line < outputL; ++line)
      {
        l_i = line / outputL * inputL;

        l = (inputL / 2 - l_i) / inputL*m_Conf.Pitch*m_Conf.TransducerElements;

        for (unsigned short sample = 0; sample < outputS; ++sample)
        {
          s_i = sample / outputS * inputS;

          part = part_multiplicator*s_i;
          maxLine = (unsigned short)std::min((l_i + part) + 1, inputL);
          minLine = (unsigned short)std::max((l_i - part), 0.0);
          VH_mult = m_Conf.TransducerElements * 2 / (maxLine - minLine);

          x = m_Conf.RecordTime / inputS * s_i * m_Conf.SpeedOfSound;
          root = l / sqrt(pow(l, 2) + pow(m_Conf.RecordTime / inputS * s_i * m_Conf.SpeedOfSound, 2));
          delayMultiplicator = root / (m_Conf.RecordTime*m_Conf.SpeedOfSound) *m_Conf.Pitch*m_Conf.TransducerElements / inputL;

          for (unsigned short l_s = minLine; l_s < maxLine; ++l_s)
          {
            AddSample = delayMultiplicator * (l_s - l_i) + s_i;
            if (AddSample < inputS && AddSample >= 0)
              m_OutputData[sample*(unsigned short)outputL + line] += m_InputData[l_s + AddSample*(unsigned short)inputL] * VonHannWindow[(unsigned short)((l_s - minLine)*VH_mult)];
          }
          m_OutputData[sample*(unsigned short)outputL + line] = m_OutputData[sample*(unsigned short)outputL + line] / (maxLine - minLine);
        }
      }
    }
    else if (m_Conf.DelayCalculationMethod == beamformingSettings::DelayCalc::QuadApprox)
    {
      //quadratic delay
      for (unsigned short line = 0; line < outputL; ++line)
      {
        l_i = line / outputL * inputL;

        for (unsigned short sample = 0; sample < outputS; ++sample)
        {
          s_i = sample / outputS * inputS;

          part = part_multiplicator*s_i;
          maxLine = (unsigned short)std::min((l_i + part) + 1, inputL);
          minLine = (unsigned short)std::max((l_i - part), 0.0);
          VH_mult = m_Conf.TransducerElements * 2 / (maxLine - minLine);

          delayMultiplicator = pow((inputS / (m_Conf.RecordTime*m_Conf.SpeedOfSound) * (m_Conf.Pitch*m_Conf.TransducerElements) / inputL), 2) / s_i;

          for (unsigned short l_s = minLine; l_s < maxLine; ++l_s)
          {
            AddSample = delayMultiplicator * pow((l_s - l_i), 2) + s_i;
            if (AddSample < inputS && AddSample >= 0) {
              m_OutputData[sample*(unsigned short)outputL + line] += m_InputData[l_s + AddSample*(unsigned short)inputL] * VonHannWindow[(unsigned short)((l_s-minLine)*VH_mult)];
            }
          }
          m_OutputData[sample*(unsigned short)outputL + line] = m_OutputData[sample*(unsigned short)outputL + line] / (maxLine - minLine);
        }
      }
    }
    else if (m_Conf.DelayCalculationMethod == beamformingSettings::DelayCalc::Spherical)
    {
      //exact delay
      for (unsigned short line = 0; line < outputL; ++line)
      {

        l_i = line / outputL * inputL;

        for (unsigned short sample = 0; sample < outputS; ++sample)
        {
          s_i = sample / outputS * inputS;

          part = part_multiplicator*s_i;
          maxLine = (unsigned short)std::min((l_i + part) + 1, inputL);
          minLine = (unsigned short)std::max((l_i - part), 0.0);
          VH_mult = m_Conf.TransducerElements * 2 / (maxLine - minLine);

          for (unsigned short l_s = minLine; l_s < maxLine; ++l_s)
          {
            AddSample = (int)sqrt(
              pow(s_i, 2)
              +
              pow((inputS / (m_Conf.RecordTime*m_Conf.SpeedOfSound) * ((l_s - l_i)*m_Conf.Pitch*m_Conf.TransducerElements) / inputL), 2)
            );
            if (AddSample < inputS && AddSample >= 0) {
              m_OutputData[sample*(unsigned short)outputL + line] += m_InputData[l_s + AddSample*(unsigned short)inputL]*VonHannWindow[(unsigned short)((l_s - minLine)*VH_mult)];
            }
          }
          m_OutputData[sample*(unsigned short)outputL + line] = m_OutputData[sample*(unsigned short)outputL + line] / (maxLine - minLine);
        }
      }
    }

    output->SetSlice(m_OutputData, i);

    delete[] m_OutputData;
    delete[] m_InputDataPuffer;
    m_OutputData = nullptr;
    m_InputData = nullptr;
  }
  m_TimeOfHeaderInitialization.Modified();

  auto end = std::chrono::high_resolution_clock::now();
  MITK_INFO << "DAS Beamforming of " << output->GetDimension(2) << " Images completed in " << ((double)std::chrono::duration_cast<std::chrono::nanoseconds>(end - begin).count()) / 1000000 << "ms" << std::endl;
}

void mitk::BeamformingDASFilter::Configure(beamformingSettings settings)
{
  m_Conf = settings;
}

double* mitk::BeamformingDASFilter::VonHannFunction(int samples)
{
  double* VonHannWindow = new double[samples];

  for (int n = 0; n < samples; ++n)
  {
    VonHannWindow[n] = (1 - cos(2 * M_PI * n / (samples - 1))) / 2;
  }

  return VonHannWindow;
}