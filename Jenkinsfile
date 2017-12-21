properties([[$class: 'BuildDiscarderProperty',
             strategy: [$class: 'LogRotator', numToKeepStr: '10']],
            pipelineTriggers([cron('H H * * *')]),
])

node('') {
    try {
        stage('Checkout') {
            checkout([$class: 'GitSCM',
                      branches: [[name: env.BRANCH_NAME]],
                      doGenerateSubmoduleConfigurations: false,
                      extensions: [[$class: 'WipeWorkspace'],
                                   [$class: 'CheckoutOption', timeout: 20],
                                   [$class: 'SubmoduleOption', parentCredentials: true, recursiveSubmodules: true, timeout: 20]],
                      submoduleCfg: [],
                      userRemoteConfigs: [[credentialsId: '39858bcb-60c5-4c11-b78d-2aca7b74b184',
                                           url: 'https://bitbucket.org/suwissagl/bldc.git']]])

        }

        stage('Build') {
            sh 'rm -fr bin'
            sh 'mkdir bin'
            timeout(30) {
                sh 'make all HW_VERSION_60=1'
            }
            sh 'cp build/BLDC_4_ChibiOS.bin bin/BLDC_4_ChibiOS.hw-v60.bin'
            sh 'make clean'
            timeout(30) {
                sh 'make all HW_VERSION_410=1'
            }
            sh 'cp build/BLDC_4_ChibiOS.bin bin/BLDC_4_ChibiOS.hw-v410.bin'
        }

        stage('Post-build') {
            env.BUILD_CUSTOM_NAME = env.BRANCH_NAME + '-' + env.BUILD_NUMBER
            env.GIT_ASKPASS = 'true'
            env.GIT_CURL_VERBOSE = 1
            withCredentials([[$class: 'UsernamePasswordMultiBinding', credentialsId: '39858bcb-60c5-4c11-b78d-2aca7b74b184',
                              usernameVariable: 'USERNAME', passwordVariable: 'PASSWORD']]) {
                try {
                    sh"git config user.name 'Jenkins CI'"
                    sh"git config user.email 'volodymyr.tymchenko@skypull.com'"
                    sh"git config credential.username $USERNAME"
                    sh"git config credential.helper '!echo password=\$PASSWORD; echo'"
                    sh"git tag -a JENKINS-${BUILD_CUSTOM_NAME} -m 'JENKINS-${BUILD_CUSTOM_NAME}'"
                    sh"git push origin --tags"
                } finally {
                    sh"git config --unset user.name"
                    sh"git config --unset user.email"
                    sh"git config --unset credential.username"
                    sh"git config --unset credential.helper"
                }

                sh 'build_all/uploadToBitbucket.bash bin/BLDC_4_ChibiOS.hw-v410.bin'
                sh 'build_all/uploadToBitbucket.bash bin/BLDC_4_ChibiOS.hw-v60.bin'
            }
        }

        currentBuild.result = 'SUCCESS'
    } catch (e) {
        currentBuild.result = "FAILURE"
        throw e
    } finally {
        step([$class: 'Mailer', notifyEveryUnstableBuild: true, recipients: 'halleck1@gmail.com', sendToIndividuals: true])
    }
}

