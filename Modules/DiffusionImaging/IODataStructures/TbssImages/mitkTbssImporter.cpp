/*=========================================================================

Program:   Medical Imaging & Interaction Toolkit
Language:  C++
Date:      $Date: 2009-07-14 19:11:20 +0200 (Tue, 14 Jul 2009) $
Version:   $Revision: 18127 $

Copyright (c) German Cancer Research Center, Division of Medical and
Biological Informatics. All rights reserved.
See MITKCopyright.txt or http://www.mitk.org/copyright.html for details.

This software is distributed WITHOUT ANY WARRANTY; without even
the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR
PURPOSE.  See the above copyright notices for more information.

=========================================================================*/

#ifndef __mitkTbssImporter_cpp
#define __mitkTbssImporter_cpp

#include "mitkTbssImporter.h"
#include <QDir>
#include <QStringList>



namespace mitk
{

  mitk::TbssImporter::TbssImporter()
  {

  }


  mitk::TbssImporter::TbssImporter(std::string path) : m_InputPath(path)
  {

  }



  mitk::TbssImage::Pointer mitk::TbssImporter::Import()
  {
    // read all images with all_*.nii.gz
    mitk::TbssImage::Pointer tbssImg = mitk::TbssImage::New();


    QDir currentDir = QDir(QString(m_InputPath.c_str()));
    currentDir.setFilter( QDir::Files );
    QStringList entries = currentDir.entryList();



    for(int e=0; e<entries.size(); e++)
    {

      std::string filename = entries.at(e).toStdString();

      if (std::string::npos != filename.find("all_FA_skeletonised.nii.gz"))
      {

        // Found a 4d skeletonized image
        if(m_InputPath.find_last_of("/") != m_InputPath.size()-1)
        {
          m_InputPath += "/";
        }

        std::string file = m_InputPath + filename;
        FileReaderType4D::Pointer reader = FileReaderType4D::New();
        reader->SetFileName(file);
        reader->Update();

        FloatImage4DType::Pointer img = FloatImage4DType::New();
        img = reader->GetOutput();

        //Create new m_Data (only once)
        if(m_Data.IsNull())
        {
          m_Data = DataImageType::New();


          FloatImage4DType::SizeType size = img->GetLargestPossibleRegion().GetSize();
          FloatImage4DType::SpacingType spacing = img->GetSpacing();


          DataImageType::SizeType dataSize;
          dataSize[0] = size[0];
          dataSize[1] = size[1];
          dataSize[2] = size[2];

          m_Data->SetRegions(dataSize);


          DataImageType::SpacingType dataSpacing;
          dataSpacing[0] = spacing[0];
          dataSpacing[1] = spacing[1];
          dataSpacing[2] = spacing[2];

          m_Data->SetSpacing(dataSpacing);

          FloatImage4DType::PointType origin = img->GetOrigin();

          DataImageType::PointType dataOrigin;
          dataOrigin[0] = origin[0];
          dataOrigin[1] = origin[1];
          dataOrigin[2] = origin[2];

          m_Data->SetOrigin(dataOrigin);


          FloatImage4DType::DirectionType dir = img->GetDirection();

          DataImageType::DirectionType dataDir;
          for(int i=0; i<=2; i++)
          {
            for(int j=0; j<=2; j++)
            {
              dataDir[i][j] = dir[i][j];
            }
          }

          m_Data->SetDirection(dataDir);

          //VariableLengthVector<float> vec;
          //vec.SetElement(i, 0.0);
          //m_Data->FillBuffer(0.0);


          // Set the length to one because otherwise allocate fails. Should be changed when groups/measurements are added
          m_Data->SetVectorLength(size[3]);
          m_Data->Allocate();



        }


        // m_Data should be allocated here so move on filling it with values from the 4D image
        FloatImage4DType::SizeType size = img->GetLargestPossibleRegion().GetSize();

        for(int i=0; i<size[0]; i++)
        {
          for(int j=0; j<size[1]; j++)
          {
            for(int k=0; k<size[2]; k++)
            {
              itk::Index<3> ix;
              ix[0] = i;
              ix[1] = j;
              ix[2] = k;
              itk::VariableLengthVector<float> pixel = m_Data->GetPixel(ix);
              int vecSize = pixel.Size();

              for(int z=0; z<size[3]; z++)
              {
                itk::Index<4> ix4;
                ix4[0] = i;
                ix4[1] = j;
                ix4[2] = k;
                ix4[3] = z;
                float value = img->GetPixel(ix4);


                pixel.SetElement(z, value);
              }
              m_Data->SetPixel(ix, pixel);
            }
          }
        }


        std::cout << "found!" << std::endl;
      }



    }


//    mitk::CastToTbssImage(m_Data.GetPointer(), tbssImg);




    tbssImg->SetGroupInfo(m_Groups);
    tbssImg->SetMeasurementInfo(m_MeasurementInfo);
    tbssImg->SetImage(m_Data);

    int vecsize = m_Data->GetVectorLength();

    tbssImg->InitializeFromVectorImage();

    return tbssImg;

  }


}



#endif // __mitkTbssImporter_cpp
