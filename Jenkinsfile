pipeline {
  agent any

  stages {
    stage('init') {
      steps {
        lastChanges(
          since: 'LAST_SUCCESSFUL_BUILD',
          format:'SIDE',
          matching: 'LINE'
        )
      }
    }

    stage('rhel{6,7}') {
      parallel {
        stage('rhel6') {
          agent {
            label 'rhel6'
          }
          stages {
            stage('rhel6.build.configure') {
              steps {
                sh '''
                  source /opt/rh/devtoolset-4/enable
                  mkdir ./build-rhel6
                  cd ./build-rhel6
                  cmake -DCMAKE_INSTALL_PREFIX=./install -DTHIRDPARTYDIR=/home/gbt1/lib_devel ..
                '''
              }
            }

            stage('rhel6.build.build') {
              steps {
                sh '''
                  source /opt/rh/devtoolset-4/enable
                  cd ./build-rhel6
                  make -j2
                '''
              }
            }

            stage('rhel6.build.install') {
              steps {
                sh '''
                  source /opt/rh/devtoolset-4/enable
                  cd ./build-rhel6
                  make install
                '''
              }
            }

            stage('rhel6.test.configure') {
              steps {
                sh '''
                  source /opt/rh/devtoolset-4/enable
                  cd ./unit_tests
                  mkdir build-rhel6
                  cd build-rhel6
                  cmake -DTHIRDPARTYDIR=/home/gbt1/lib_devel ..
                '''
              }
            }

            stage('rhel6.test.build') {
              steps {
                sh '''
                  source /opt/rh/devtoolset-4/enable
                  cd ./unit_tests/build-rhel6
                  make -j2
                '''
              }
            }

            stage('rhel6.test.test') {
              steps {
                sh '''
                  source /opt/rh/devtoolset-4/enable
                  cd ./unit_tests/build-rhel6
                  LD_LIBRARY_PATH=/home/gbt1/lib_devel/lib:$LD_LIBRARY_PATH ./matrix_test
                '''
              }
            }
          }
        }

        stage('rhel7') {
          agent {
            label 'rhel7'
          }
          stages {
            stage('rhel7.build.configure') {
              steps {
                sh '''
                  source /opt/rh/devtoolset-7/enable
                  mkdir ./build-rhel7
                  cd ./build-rhel7
                  cmake -DCMAKE_INSTALL_PREFIX=./install -DTHIRDPARTYDIR=/home/gbt1/lib_devel ..
                '''
              }
            }

            stage('rhel7.build.build') {
              steps {
                sh '''
                  source /opt/rh/devtoolset-7/enable
                  cd ./build-rhel7
                  make -j2
                '''
              }
            }

            stage('rhel7.build.install') {
              steps {
                sh '''
                  source /opt/rh/devtoolset-7/enable
                  cd ./build-rhel7
                  make install
                '''
              }
            }

            stage('rhel7.test.configure') {
              steps {
                sh '''
                  source /opt/rh/devtoolset-7/enable
                  cd ./unit_tests
                  mkdir build-rhel7
                  cd build-rhel7
                  cmake -DTHIRDPARTYDIR=/home/gbt1/lib_devel ..
                '''
              }
            }

            stage('rhel7.test.build') {
              steps {
                sh '''
                  source /opt/rh/devtoolset-7/enable
                  cd ./unit_tests/build-rhel7
                  make -j2
                '''
              }
            }

            stage('rhel7.test.test') {
              steps {
                sh '''
                  source /opt/rh/devtoolset-7/enable
                  cd ./unit_tests/build-rhel7
                  LD_LIBRARY_PATH=/home/gbt1/lib_devel/lib:$LD_LIBRARY_PATH ./matrix_test
                '''
              }
            }
          }
        }
      }
    }
  }

  post {
    regression {
        script { env.CHANGED = true }
    }

    fixed {
        script { env.CHANGED = true }
    }

    cleanup {
        do_notify()
    }  
  }
}
